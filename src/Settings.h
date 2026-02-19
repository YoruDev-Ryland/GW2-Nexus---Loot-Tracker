#pragma once
#include <string>

// ── Settings persisted to <addondir>/settings.json ───────────────────────────
struct Settings
{
    std::string ApiKey;             // GW2 API key (requires "inventories + wallet")
    int         PollIntervalSec = 15; // How often to query the GW2 API
    bool        ShowWindow      = true;
    bool        ShowZeroDeltas  = false; // Include items with no change in the list
    bool        TrackCurrency   = true;
    bool        TrackItems      = true;

    // Load from / save to disk.  Path is resolved via APIDefs->Paths_GetAddonDirectory.
    void Load();
    void Save() const;
};

extern Settings g_Settings;
