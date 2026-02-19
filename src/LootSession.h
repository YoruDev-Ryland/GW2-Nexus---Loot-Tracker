#pragma once
#include "GW2Api.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

namespace LootSession
{
    // ── Delta types shown in the UI ───────────────────────────────────────────

    struct ItemDelta
    {
        int         id;
        std::string name;
        std::string rarity;       // "Fine", "Rare", "Exotic", etc.
        std::string chatLink;
        int         delta;        // positive = gained, negative = lost
        // Texture identifier registered with Nexus Texture API.
        // Empty until the icon has been loaded asynchronously.
        std::string textureId;
        std::string description;  // optional flavour/lore text
        std::string type;         // item type, e.g. "Weapon", "Armor"
        int         vendorValue = 0; // copper coins
    };

    struct CurrencyDelta
    {
        int         id;
        std::string name;
        int64_t     delta;
        std::string textureId;
    };

    // ── Session state accessible to the UI ───────────────────────────────────

    // Returns a thread-safe copy of the current item deltas.
    std::vector<ItemDelta>     GetItemDeltas();
    // Returns a thread-safe copy of the current currency deltas.
    std::vector<CurrencyDelta> GetCurrencyDeltas();

    // ── Known item/currency database (grows over playtime) ────────────────────
    // Used by the profile editor to show what can be tracked.
    struct KnownItem
    {
        int         id;
        std::string name;
        std::string type;
        std::string rarity;
        std::string textureId; // "LT_ITEM_{id}"
    };
    struct KnownCurrency
    {
        int         id;
        std::string name;
        std::string textureId; // "LT_CURRENCY_{id}"
    };
    std::vector<KnownItem>     GetKnownItems();
    std::vector<KnownCurrency> GetKnownCurrencies();

    // How long the current session has been running (0 if not active).
    std::chrono::seconds ElapsedTime();

    bool IsActive();

    // ── Session lifecycle ─────────────────────────────────────────────────────

    // Initialize: start polling & prime the baseline on first response.
    void Init();

    // Start a fresh session: resets all deltas and records new baseline.
    void Start();

    // Pause accumulation (polling keeps running so baseline stays warm).
    void Stop();

    // Check auto-start conditions and start a new session if triggered.
    // Called from the polling thread each poll cycle.
    void CheckAutoStart();

    // Called internally by GW2Api polling thread with a fresh snapshot.
    void OnSnapshot(GW2Api::Snapshot snap);

    void Shutdown();
}
