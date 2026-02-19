#include "LootSession.h"
#include "GW2Api.h"
#include "Settings.h"
#include "Shared.h"

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <algorithm>

using Clock   = std::chrono::steady_clock;
using Seconds = std::chrono::seconds;

// ── Internal state ─────────────────────────────────────────────────────────────

static std::mutex s_Mutex;

static bool  s_Active    = false;
static bool  s_HasBase   = false; // true once the first snapshot has been received

static Clock::time_point s_StartTime;

// Baseline snapshots (taken on session start).
static std::unordered_map<int, int64_t> s_BaseWallet;  // currency id -> value
static std::unordered_map<int, int>     s_BaseItems;   // item id     -> count

// Accumulated deltas since the session started.
static std::unordered_map<int, int64_t> s_DeltaWallet;
static std::unordered_map<int, int>     s_DeltaItems;

// Resolved info cache (filled asynchronously from FetchItemDetails).
static std::unordered_map<int, GW2Api::ItemInfo>     s_ItemInfo;
static std::unordered_map<int, GW2Api::CurrencyInfo> s_CurrencyInfo;

// IDs waiting for their info to be fetched.
static std::unordered_set<int> s_PendingItemIds;
static std::unordered_set<int> s_PendingCurrencyIds;

// ── Info resolution helpers ───────────────────────────────────────────────────

// Fetches item/currency info for any IDs we haven't resolved yet.
// Called from the snapshot thread — no ImGui interaction here.
static void ResolveNewIds()
{
    std::vector<int> needItems, needCurrencies;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        needItems      = std::vector<int>(s_PendingItemIds.begin(),     s_PendingItemIds.end());
        needCurrencies = std::vector<int>(s_PendingCurrencyIds.begin(), s_PendingCurrencyIds.end());
    }

    if (!needItems.empty())
    {
        auto infos = GW2Api::FetchItemDetails(needItems);
        std::lock_guard<std::mutex> lock(s_Mutex);
        for (auto& i : infos)
        {
            s_ItemInfo[i.id] = i;
            s_PendingItemIds.erase(i.id);

            // Register icon texture with Nexus (async; no callback needed here)
            if (APIDefs && !i.iconUrl.empty())
            {
                std::string texId = "LT_ITEM_" + std::to_string(i.id);
                // Split URL into host + path for LoadTextureFromURL
                // icon URLs look like:
                // https://render.guildwars2.com/file/<hash>/<id>.png
                const std::string host = "https://render.guildwars2.com";
                std::string path = i.iconUrl;
                // Strip the host prefix if present
                if (path.rfind(host, 0) == 0)
                    path = path.substr(host.size());

                APIDefs->Textures_LoadFromURL(texId.c_str(),
                    "https://render.guildwars2.com",
                    path.c_str(),
                    nullptr); // no callback — UI polls Textures_Get each frame
            }
        }
    }

    if (!needCurrencies.empty())
    {
        auto infos = GW2Api::FetchCurrencyDetails(needCurrencies);
        std::lock_guard<std::mutex> lock(s_Mutex);
        for (auto& c : infos)
        {
            s_CurrencyInfo[c.id] = c;
            s_PendingCurrencyIds.erase(c.id);

            if (APIDefs && !c.iconUrl.empty())
            {
                std::string texId = "LT_CURRENCY_" + std::to_string(c.id);
                const std::string host = "https://render.guildwars2.com";
                std::string path = c.iconUrl;
                if (path.rfind(host, 0) == 0)
                    path = path.substr(host.size());

                APIDefs->Textures_LoadFromURL(texId.c_str(),
                    "https://render.guildwars2.com",
                    path.c_str(),
                    nullptr);
            }
        }
    }
}

// ── Public API ─────────────────────────────────────────────────────────────────

void LootSession::Init()
{
    // Wire the polling thread callback.
    GW2Api::StartPolling([](GW2Api::Snapshot snap)
    {
        LootSession::OnSnapshot(std::move(snap));
    });
}

void LootSession::Start()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_DeltaWallet.clear();
    s_DeltaItems.clear();
    s_Active    = s_HasBase; // can't start until first snapshot arrived
    s_StartTime = Clock::now();
    if (APIDefs)
        APIDefs->Log(LOGL_INFO, "LootTracker", "Session started.");
}

void LootSession::Stop()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Active = false;
    if (APIDefs)
        APIDefs->Log(LOGL_INFO, "LootTracker", "Session stopped.");
}

void LootSession::OnSnapshot(GW2Api::Snapshot snap)
{
    bool needsResolve = false;

    // ── Phase 1: apply snapshot under the lock ────────────────────────────────
    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        // Build lookup maps for the new snapshot
        std::unordered_map<int, int64_t> newWallet;
        for (auto& w : snap.wallet)
            newWallet[w.id] = w.value;

        std::unordered_map<int, int> newItems;
        for (auto& item : snap.inventory)
            newItems[item.id] += item.count;

        if (!s_HasBase)
        {
            // First ever snapshot — record as baseline, don't accumulate.
            s_BaseWallet = newWallet;
            s_BaseItems  = newItems;
            s_HasBase    = true;

            // Queue all currencies for info fetch on first run
            for (auto& [id, _] : newWallet)
                if (s_CurrencyInfo.find(id) == s_CurrencyInfo.end())
                    s_PendingCurrencyIds.insert(id);
        }
        else if (s_Active)
        {
            // Compute deltas relative to baseline this session
            for (auto& [id, val] : newWallet)
            {
                int64_t base = 0;
                auto it = s_BaseWallet.find(id);
                if (it != s_BaseWallet.end()) base = it->second;
                s_DeltaWallet[id] = val - base;

                if (s_CurrencyInfo.find(id) == s_CurrencyInfo.end())
                    s_PendingCurrencyIds.insert(id);
            }

            for (auto& [id, cnt] : newItems)
            {
                int base = 0;
                auto it = s_BaseItems.find(id);
                if (it != s_BaseItems.end()) base = it->second;
                int d = cnt - base;
                if (d != 0)
                {
                    s_DeltaItems[id] = d;
                    if (s_ItemInfo.find(id) == s_ItemInfo.end())
                        s_PendingItemIds.insert(id);
                }
            }
            // Items that were at baseline but not in the new snapshot (fully gone)
            for (auto& [id, base] : s_BaseItems)
            {
                if (newItems.find(id) == newItems.end())
                {
                    s_DeltaItems[id] = -base;
                    if (s_ItemInfo.find(id) == s_ItemInfo.end())
                        s_PendingItemIds.insert(id);
                }
            }
        }

        needsResolve = !s_PendingItemIds.empty() || !s_PendingCurrencyIds.empty();
    } // lock released here

    // ── Phase 2: resolve new IDs without holding the lock (HTTP calls block) ──
    if (needsResolve)
        ResolveNewIds();
}

void LootSession::Shutdown()
{
    GW2Api::StopPolling();
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Active  = false;
    s_HasBase = false;
    s_DeltaWallet.clear();
    s_DeltaItems.clear();
}

bool LootSession::IsActive()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Active;
}

std::chrono::seconds LootSession::ElapsedTime()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (!s_Active) return Seconds(0);
    return std::chrono::duration_cast<Seconds>(Clock::now() - s_StartTime);
}

std::vector<LootSession::ItemDelta> LootSession::GetItemDeltas()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    std::vector<ItemDelta> result;
    result.reserve(s_DeltaItems.size());

    for (auto& [id, delta] : s_DeltaItems)
    {
        ItemDelta d;
        d.id    = id;
        d.delta = delta;

        auto it = s_ItemInfo.find(id);
        if (it != s_ItemInfo.end())
        {
            d.name     = it->second.name;
            d.rarity   = it->second.rarity;
            d.chatLink = it->second.chatLink;
        }
        else
        {
            d.name = "Item #" + std::to_string(id);
        }

        d.textureId = "LT_ITEM_" + std::to_string(id);
        result.push_back(std::move(d));
    }

    // Sort: gained first (descending delta), then losses
    std::sort(result.begin(), result.end(),
        [](const ItemDelta& a, const ItemDelta& b){ return a.delta > b.delta; });

    return result;
}

std::vector<LootSession::CurrencyDelta> LootSession::GetCurrencyDeltas()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    std::vector<CurrencyDelta> result;
    result.reserve(s_DeltaWallet.size());

    for (auto& [id, delta] : s_DeltaWallet)
    {
        if (delta == 0) continue;

        CurrencyDelta d;
        d.id    = id;
        d.delta = delta;

        auto it = s_CurrencyInfo.find(id);
        if (it != s_CurrencyInfo.end())
            d.name = it->second.name;
        else
            d.name = "Currency #" + std::to_string(id);

        d.textureId = "LT_CURRENCY_" + std::to_string(id);
        result.push_back(std::move(d));
    }

    std::sort(result.begin(), result.end(),
        [](const CurrencyDelta& a, const CurrencyDelta& b){ return a.delta > b.delta; });

    return result;
}
