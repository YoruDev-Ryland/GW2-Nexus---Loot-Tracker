#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace GW2Api
{
    // ── Data structures returned by the API ──────────────────────────────────

    struct WalletEntry
    {
        int id;
        int64_t value;
    };

    struct ItemStack
    {
        int id;
        int count;
        int slot; // bag slot index
    };

    struct ItemInfo
    {
        int         id;
        std::string name;
        std::string rarity;       // "Junk", "Basic", "Fine", "Masterwork", ...
        std::string iconUrl;      // full URL to render.guildwars2.com icon
        std::string chatLink;
        std::string description;  // optional lore/flavour text
        std::string type;         // "Weapon", "Armor", "Consumable", etc.
        int         vendorValue = 0; // copper coins vendor price
    };

    struct Snapshot
    {
        std::vector<WalletEntry> wallet;
        std::vector<ItemStack>   inventory; // character + account bank combined
    };

    // ── API key validation result ─────────────────────────────────────────────
    enum class KeyStatus
    {
        Unknown,
        Valid,
        Invalid,
        NoPermissions, // key exists but missing "inventories" or "wallet" scope
    };

    // ── Callbacks ─────────────────────────────────────────────────────────────
    // Called from background thread — do NOT call ImGui from here.
    using SnapshotCallback = std::function<void(Snapshot)>;

    // ── Public API ────────────────────────────────────────────────────────────

    // Validate the api key and return its status.  Blocking, call from BG thread.
    KeyStatus ValidateKey(const std::string& apiKey);

    // Fetch a full snapshot (wallet + inventory).  Blocking, call from BG thread.
    bool FetchSnapshot(const std::string& apiKey,
                       const std::string& characterName,
                       Snapshot&          outSnapshot);

    // Fetch item details for a batch of IDs (max 200 per call).
    // Returns only the successfully fetched entries.
    std::vector<ItemInfo> FetchItemDetails(const std::vector<int>& ids);

    // Fetch currency (wallet currency type) name + icon for a set of IDs.
    struct CurrencyInfo
    {
        int         id;
        std::string name;
        std::string iconUrl;
    };
    std::vector<CurrencyInfo> FetchCurrencyDetails(const std::vector<int>& ids);

    // Start the background polling thread.
    // onNewSnapshot is called every PollIntervalSec seconds.
    void StartPolling(SnapshotCallback onNewSnapshot);

    // Stop + join the polling thread.  Safe to call multiple times.
    void StopPolling();

    // Poke the polling thread to fire immediately (e.g., on session start).
    void PollNow();

    // Returns true if the background thread is running.
    bool IsPolling();
}
