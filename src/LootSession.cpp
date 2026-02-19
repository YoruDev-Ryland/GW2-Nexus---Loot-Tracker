#include "LootSession.h"
#include "GW2Api.h"
#include "Settings.h"
#include "Shared.h"
#include "SessionHistory.h"

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <ctime>
#include <algorithm>

using Clock   = std::chrono::steady_clock;
using Seconds = std::chrono::seconds;

// ── Internal state ─────────────────────────────────────────────────────────────

static std::mutex s_Mutex;

static bool  s_Active    = false;
static bool  s_HasBase   = false; // true once the first snapshot has been received

static Clock::time_point s_StartTime;
static std::chrono::system_clock::time_point s_StartWallTime;

// Auto-start tracking
static uint32_t s_LastMapId    = 0;
static int      s_LastAutoHour = -1;
static int      s_LastAutoDay  = -1;

// Baseline snapshots (taken on session start).
static std::unordered_map<int, int64_t> s_BaseWallet;  // currency id -> value
static std::unordered_map<int, int>     s_BaseItems;   // item id     -> count

// Set to true by Start() so the very next snapshot is recorded as the new
// baseline rather than diffed against the old one.
static bool s_NeedsNewBase = false;

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
        LootSession::CheckAutoStart();
        LootSession::OnSnapshot(std::move(snap));
    });
}

void LootSession::Start()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_DeltaWallet.clear();
    s_DeltaItems.clear();
    // Mark that the next snapshot should become the new baseline rather than
    // being diffed against potentially stale data.  s_Active = true immediately
    // so the UI shows the Stop button and the timer starts.
    s_Active       = true;
    s_NeedsNewBase = true;
    s_StartTime     = Clock::now();
    s_StartWallTime = std::chrono::system_clock::now();
    if (APIDefs)
        APIDefs->Log(LOGL_INFO, "LootTracker", "Session started — waiting for baseline snapshot.");
}

void LootSession::Stop()
{
    bool wasActive = false;
    std::chrono::system_clock::time_point wallStart;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        wasActive = s_Active;
        wallStart = s_StartWallTime;
        s_Active  = false;
    }

    if (wasActive)
    {
        // GetItemDeltas/GetCurrencyDeltas re-acquire the mutex internally,
        // so they must be called OUTSIDE the lock block above.
        auto items      = GetItemDeltas();
        auto currencies = GetCurrencyDeltas();
        SessionHistory::SaveSession(wallStart,
                                    std::chrono::system_clock::now(),
                                    std::move(items),
                                    std::move(currencies));
    }

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

        if (!s_HasBase || s_NeedsNewBase)
        {
            // Snapshot is a fresh baseline (first ever, or user clicked Start/Reset).
            s_BaseWallet   = newWallet;
            s_BaseItems    = newItems;
            s_HasBase      = true;
            s_NeedsNewBase = false;
            // Don't force s_Active here — on the very first ever snapshot we
            // just prime the baseline.  When the user clicks Start, s_Active is
            // already true by the time the baseline snapshot arrives.

            // Queue all currencies for info fetch
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
    s_Active       = false;
    s_HasBase      = false;
    s_NeedsNewBase = false;
    s_DeltaWallet.clear();
    s_DeltaItems.clear();
}

bool LootSession::IsActive()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Active;
}

// ── Auto-start ────────────────────────────────────────────────────────────────

void LootSession::CheckAutoStart()
{
    if (g_Settings.AutoStart == AutoStartMode::Disabled) return;

    // Determine current UTC time
    auto nowRaw = std::chrono::system_clock::now();
    std::time_t tnow = std::chrono::system_clock::to_time_t(nowRaw);
    std::tm utc{};
    gmtime_s(&utc, &tnow);
    int currentHour = utc.tm_hour;
    int currentDay  = utc.tm_yday;

    // Current in-game map ID (0 = character select / not loaded yet)
    uint32_t currentMapId = 0;
    if (MumbleLink) currentMapId = MumbleLink->Context.MapId;

    bool shouldStart = false;

    switch (g_Settings.AutoStart)
    {
    case AutoStartMode::OnLogin:
        // Trigger when transitioning from map 0 (loading / char select) → in a map
        if (s_LastMapId == 0 && currentMapId != 0)
            shouldStart = true;
        break;

    case AutoStartMode::Hourly:
        if (s_LastAutoHour < 0)
            s_LastAutoHour = currentHour; // first-time initialise — don't fire yet
        else if (currentHour != s_LastAutoHour)
            shouldStart = true;
        break;

    case AutoStartMode::Daily:
        if (s_LastAutoDay < 0)
            s_LastAutoDay = currentDay; // first-time initialise — don't fire yet
        else if (currentDay != s_LastAutoDay)
            shouldStart = true;
        break;

    default:
        break;
    }

    s_LastMapId = currentMapId;

    if (shouldStart)
    {
        s_LastAutoHour = currentHour;
        s_LastAutoDay  = currentDay;
        // Stop() saves the current session to history; Start() primes a new baseline.
        Stop();
        Start();
        if (APIDefs)
            APIDefs->Log(LOGL_INFO, "LootTracker", "Auto-start: new session begun.");
    }
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
            d.name        = it->second.name;
            d.rarity      = it->second.rarity;
            d.chatLink    = it->second.chatLink;
            d.description = it->second.description;
            d.type        = it->second.type;
            d.vendorValue = it->second.vendorValue;
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
