#pragma once
#include <string>

// ── Auto-start modes ──────────────────────────────────────────────────────────
enum class AutoStartMode
{
    Disabled = 0,
    OnLogin  = 1,  // start a new session each time you enter the game world
    Hourly   = 2,  // reset at the top of every UTC hour
    Daily    = 3,  // reset at GW2 daily reset (00:00 UTC)
};

// ── Settings persisted to <addondir>/settings.json ───────────────────────────
struct Settings
{
    std::string   ApiKey;               // GW2 API key (requires "inventories + wallet")
    int           PollIntervalSec = 30; // How often to query the GW2 API
    bool          ShowWindow      = true;
    bool          ShowZeroDeltas  = false; // Include items with no change in the list
    bool          TrackCurrency   = true;
    bool          TrackItems      = true;
    AutoStartMode AutoStart       = AutoStartMode::Disabled;

    // Load from / save to disk.  Path is resolved via APIDefs->Paths_GetAddonDirectory.
    void Load();
    void Save() const;
};

extern Settings g_Settings;
