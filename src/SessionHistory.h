#pragma once
#include "LootSession.h"

#include <string>
#include <vector>
#include <chrono>

namespace SessionHistory
{
    // ── One completed loot session saved to disk ───────────────────────────────
    struct SavedSession
    {
        std::string label;          // human-readable name, e.g. "Session 3"
        std::string startTimestamp; // ISO-8601 UTC, e.g. "2025-04-07T12:00:00Z"
        std::string endTimestamp;
        std::vector<LootSession::ItemDelta>     items;
        std::vector<LootSession::CurrencyDelta> currencies;
    };

    // Save the current finished session.  Called from Stop().
    // start / end are wall-clock UTC times.
    void SaveSession(std::chrono::system_clock::time_point start,
                     std::chrono::system_clock::time_point end,
                     std::vector<LootSession::ItemDelta>     items,
                     std::vector<LootSession::CurrencyDelta> currencies);

    // Reload history from disk and return all sessions (newest first).
    std::vector<SavedSession> GetAll();

    // Load history from disk (called once at addon init).
    void Load();
}
