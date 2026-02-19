#pragma once
#include <string>
#include <vector>
#include <unordered_set>

// ── Tracking modes ─────────────────────────────────────────────────────────────
enum class TrackingMode { All = 0, Custom = 1 };

// ── A named preset of items + currencies to display ───────────────────────────
struct TrackingProfile
{
    std::string             name;
    std::unordered_set<int> itemIds;     // IDs to show; when empty = show all
    std::unordered_set<int> currencyIds; // IDs to show; when empty = show all
};

namespace TrackingFilter
{
    // ── Mode & active profile ──────────────────────────────────────────────────
    TrackingMode GetMode();
    void         SetMode(TrackingMode m);

    // Active profile index into GetProfilesCopy(); -1 = "All" (no filter).
    int  GetActiveProfileIndex();
    void SetActiveProfile(int index); // pass -1 to return to "All"

    // ── Filter queries (thread-safe) ───────────────────────────────────────────
    // Returns true when the current mode / profile allows this id through.
    bool IsItemTracked(int id);
    bool IsCurrencyTracked(int id);

    // ── Profile CRUD ───────────────────────────────────────────────────────────
    // Returns a snapshot copy — safe to use without holding the internal lock.
    std::vector<TrackingProfile> GetProfilesCopy();

    // Create a new (empty) profile and return its index.
    int  NewProfile(const std::string& name);
    void DeleteProfile(int index);
    // Replaces the stored profile at index with p.
    void UpdateProfile(int index, const TrackingProfile& p);

    // ── Persistence ────────────────────────────────────────────────────────────
    void Load();
    void Save();
}
