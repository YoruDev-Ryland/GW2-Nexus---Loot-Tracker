#include "GW2Api.h"
#include "Settings.h"
#include "Shared.h"

#include <nlohmann/json.hpp>
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>

using json = nlohmann::json;

// ── WinHTTP helpers ──────────────────────────────────────────────────────────

static const wchar_t* GW2_HOST    = L"api.guildwars2.com";
static const int      GW2_PORT    = INTERNET_DEFAULT_HTTPS_PORT;

// Performs a HTTPS GET to https://api.guildwars2.com/<path>
// with an optional "Authorization: Bearer <apiKey>" header.
// Returns the response body as UTF-8 string, or empty string on failure.
static std::string HttpGet(const std::wstring& path, const std::string& apiKey = "")
{
    std::string result;

    HINTERNET hSession = WinHttpOpen(
        L"LootTracker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, GW2_HOST, GW2_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hReq = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hReq)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Add auth header if key provided
    if (!apiKey.empty())
    {
        std::wstring authHeader = L"Authorization: Bearer " +
            std::wstring(apiKey.begin(), apiKey.end());
        WinHttpAddRequestHeaders(hReq, authHeader.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL sent = WinHttpSendRequest(hReq,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (sent && WinHttpReceiveResponse(hReq, nullptr))
    {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode, &statusSize,
            WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200)
        {
            DWORD bytesAvail = 0;
            while (WinHttpQueryDataAvailable(hReq, &bytesAvail) && bytesAvail > 0)
            {
                std::vector<char> buf(bytesAvail + 1, '\0');
                DWORD bytesRead = 0;
                WinHttpReadData(hReq, buf.data(), bytesAvail, &bytesRead);
                result.append(buf.data(), bytesRead);
            }
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// Build a query URL like /v2/items?ids=1,2,3&lang=en
static std::wstring BuildIdsPath(const std::wstring& endpoint,
                                 const std::vector<int>& ids)
{
    std::wostringstream ss;
    ss << endpoint << L"?ids=";
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i > 0) ss << L',';
        ss << ids[i];
    }
    ss << L"&lang=en";
    return ss.str();
}

// ── Public API implementations ────────────────────────────────────────────────

GW2Api::KeyStatus GW2Api::ValidateKey(const std::string& apiKey)
{
    if (apiKey.empty()) return KeyStatus::Invalid;

    std::string body = HttpGet(L"/v2/tokeninfo", apiKey);
    if (body.empty()) return KeyStatus::Invalid;

    try
    {
        json j = json::parse(body);
        if (j.contains("text")) return KeyStatus::Invalid; // API returned error

        auto perms = j.value("permissions", json::array());
        bool hasInventory = false, hasWallet = false;
        for (auto& p : perms)
        {
            std::string s = p.get<std::string>();
            if (s == "inventories") hasInventory = true;
            if (s == "wallet")      hasWallet    = true;
        }
        if (!hasInventory || !hasWallet) return KeyStatus::NoPermissions;
        return KeyStatus::Valid;
    }
    catch (...) { return KeyStatus::Invalid; }
}

bool GW2Api::FetchSnapshot(const std::string& apiKey,
                            const std::string& characterName,
                            Snapshot&          out)
{
    // ── Wallet ────────────────────────────────────────────────────────────────
    {
        std::string body = HttpGet(L"/v2/account/wallet", apiKey);
        if (body.empty()) return false;
        try
        {
            json j = json::parse(body);
            out.wallet.clear();
            for (auto& entry : j)
                out.wallet.push_back({ entry["id"].get<int>(),
                                       entry["value"].get<int64_t>() });
        }
        catch (...) { return false; }
    }

    // ── Character inventory ───────────────────────────────────────────────────
    out.inventory.clear();
    if (!characterName.empty())
    {
        // URL-encode the character name (spaces -> %20, etc.)
        std::string encoded;
        for (char c : characterName)
        {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                encoded += c;
            else
            {
                char buf[4];
                sprintf_s(buf, "%%%02X", static_cast<unsigned char>(c));
                encoded += buf;
            }
        }

        std::wstring path = L"/v2/characters/" +
            std::wstring(encoded.begin(), encoded.end()) + L"/inventory";

        std::string body = HttpGet(path, apiKey);
        if (!body.empty())
        {
            try
            {
                json j = json::parse(body);
                int slot = 0;
                for (auto& bag : j["bags"])
                {
                    if (bag.is_null()) { ++slot; continue; }
                    for (auto& item : bag["inventory"])
                    {
                        if (!item.is_null())
                            out.inventory.push_back({
                                item["id"].get<int>(),
                                item["count"].get<int>(),
                                slot });
                        ++slot;
                    }
                }
            }
            catch (...) { /* partial failure ok — wallet already fetched */ }
        }
    }

    // ── Material storage ─────────────────────────────────────────────────────
    // Merging material storage counts into inventory means that auto-deposit
    // (items moving from bags to material storage) doesn't show as a negative
    // delta — only true account-wide gains/losses are reflected.
    {
        std::string body = HttpGet(L"/v2/account/materials", apiKey);
        if (!body.empty())
        {
            try
            {
                // Build a quick lookup index into out.inventory
                std::unordered_map<int, size_t> idx;
                idx.reserve(out.inventory.size());
                for (size_t i = 0; i < out.inventory.size(); ++i)
                    idx[out.inventory[i].id] = i;

                json j = json::parse(body);
                for (auto& entry : j)
                {
                    int id    = entry["id"].get<int>();
                    int count = entry["count"].get<int>();
                    if (count <= 0) continue;

                    auto it = idx.find(id);
                    if (it != idx.end())
                        out.inventory[it->second].count += count;
                    else
                    {
                        idx[id] = out.inventory.size();
                        out.inventory.push_back({ id, count, -1 }); // slot -1 = material storage
                    }
                }
            }
            catch (...) { /* partial failure ok */ }
        }
    }

    // ── Account bank ─────────────────────────────────────────────────────────
    // Merging bank prevents items moved from bags to bank showing as losses.
    {
        std::string body = HttpGet(L"/v2/account/bank", apiKey);
        if (!body.empty())
        {
            try
            {
                std::unordered_map<int, size_t> idx;
                for (size_t i = 0; i < out.inventory.size(); ++i)
                    idx[out.inventory[i].id] = i;

                json j = json::parse(body);
                for (auto& slot : j)
                {
                    if (slot.is_null()) continue;
                    int id    = slot["id"].get<int>();
                    int count = slot.value("count", 0);
                    if (count <= 0) continue;
                    auto it = idx.find(id);
                    if (it != idx.end())
                        out.inventory[it->second].count += count;
                    else
                    {
                        idx[id] = out.inventory.size();
                        out.inventory.push_back({ id, count, -2 }); // slot -2 = bank
                    }
                }
            }
            catch (...) { /* partial failure ok */ }
        }
    }

    // ── Shared inventory slots (gem-store bags) ───────────────────────────────
    {
        std::string body = HttpGet(L"/v2/account/inventory", apiKey);
        if (!body.empty())
        {
            try
            {
                std::unordered_map<int, size_t> idx;
                for (size_t i = 0; i < out.inventory.size(); ++i)
                    idx[out.inventory[i].id] = i;

                json j = json::parse(body);
                for (auto& slot : j)
                {
                    if (slot.is_null()) continue;
                    int id    = slot["id"].get<int>();
                    int count = slot.value("count", 0);
                    if (count <= 0) continue;
                    auto it = idx.find(id);
                    if (it != idx.end())
                        out.inventory[it->second].count += count;
                    else
                    {
                        idx[id] = out.inventory.size();
                        out.inventory.push_back({ id, count, -3 }); // slot -3 = shared
                    }
                }
            }
            catch (...) { /* partial failure ok */ }
        }
    }

    return true;
}

std::vector<GW2Api::ItemInfo> GW2Api::FetchItemDetails(const std::vector<int>& ids)
{
    std::vector<ItemInfo> result;
    if (ids.empty()) return result;

    // The GW2 API accepts at most 200 IDs per request
    for (size_t offset = 0; offset < ids.size(); offset += 200)
    {
        size_t end = std::min(offset + 200, ids.size());
        std::vector<int> batch(ids.begin() + offset, ids.begin() + end);

        std::string body = HttpGet(BuildIdsPath(L"/v2/items", batch));
        if (body.empty()) continue;

        try
        {
            json j = json::parse(body);
            for (auto& item : j)
            {
                ItemInfo info;
                info.id          = item.value("id",           0);
                info.name        = item.value("name",         "");
                info.rarity      = item.value("rarity",       "");
                info.iconUrl     = item.value("icon",         "");
                info.chatLink    = item.value("chat_link",    "");
                info.description = item.value("description",  "");
                info.type        = item.value("type",         "");
                info.vendorValue = item.value("vendor_value",  0);
                result.push_back(std::move(info));
            }
        }
        catch (...) {}
    }

    return result;
}

std::vector<GW2Api::CurrencyInfo> GW2Api::FetchCurrencyDetails(const std::vector<int>& ids)
{
    std::vector<CurrencyInfo> result;
    if (ids.empty()) return result;

    std::string body = HttpGet(BuildIdsPath(L"/v2/currencies", ids));
    if (body.empty()) return result;

    try
    {
        json j = json::parse(body);
        for (auto& cur : j)
        {
            CurrencyInfo info;
            info.id      = cur.value("id",   0);
            info.name    = cur.value("name", "");
            info.iconUrl = cur.value("icon", "");
            result.push_back(std::move(info));
        }
    }
    catch (...) {}

    return result;
}

std::vector<GW2Api::CurrencyInfo> GW2Api::FetchAllCurrencies()
{
    // /v2/currencies with no IDs returns an array of all currency IDs
    std::string body = HttpGet(L"/v2/currencies");
    if (body.empty()) return {};

    try
    {
        json j = json::parse(body);
        std::vector<int> ids;
        ids.reserve(j.size());
        for (auto& el : j) ids.push_back(el.get<int>());
        return FetchCurrencyDetails(ids);
    }
    catch (...) { return {}; }
}

// ── Background polling thread ─────────────────────────────────────────────────

static std::thread             s_PollThread;
static std::atomic<bool>       s_Running   { false };
static std::mutex              s_Mutex;
static std::condition_variable s_Cv;
static bool                    s_PollNow   { false };
static GW2Api::SnapshotCallback s_Callback;

void GW2Api::StartPolling(SnapshotCallback onNewSnapshot)
{
    if (s_Running.exchange(true)) return; // already running

    s_Callback = std::move(onNewSnapshot);

    s_PollThread = std::thread([]()
    {
        while (s_Running.load())
        {
            // Respect the configured interval, but allow early wakeup via PollNow()
            {
                std::unique_lock<std::mutex> lock(s_Mutex);
                s_Cv.wait_for(lock,
                    std::chrono::seconds(g_Settings.PollIntervalSec),
                    []{ return s_PollNow || !s_Running.load(); });
                s_PollNow = false;
            }

            if (!s_Running.load()) break;

            // Skip if no API key or no character name yet
            if (g_Settings.ApiKey.empty()) continue;

            std::string charName;
            if (MumbleIdent) charName = MumbleIdent->Name;

            Snapshot snap;
            if (FetchSnapshot(g_Settings.ApiKey, charName, snap))
            {
                if (s_Callback) s_Callback(std::move(snap));
            }
        }
    });
}

void GW2Api::StopPolling()
{
    if (!s_Running.exchange(false)) return; // wasn't running

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_PollNow = true;
    }
    s_Cv.notify_all();

    if (s_PollThread.joinable())
        s_PollThread.join();
}

void GW2Api::PollNow()
{
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_PollNow = true;
    }
    s_Cv.notify_all();
}

bool GW2Api::IsPolling()
{
    return s_Running.load();
}
