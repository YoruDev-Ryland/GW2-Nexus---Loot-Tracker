#include "Settings.h"
#include "Shared.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

using json = nlohmann::json;

Settings g_Settings;

// Returns the full path to settings.json, e.g.
// "C:\...\Guild Wars 2\addons\LootTracker\settings.json"
static std::string SettingsPath()
{
    // APIDefs may not be set yet during very early init — guard against that.
    if (!APIDefs || !APIDefs->Paths_GetAddonDirectory)
        return "";

    std::string dir = APIDefs->Paths_GetAddonDirectory("LootTracker");
    // Ensure the directory exists
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\settings.json";
}

void Settings::Load()
{
    std::string path = SettingsPath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) return; // first run — use defaults

    try
    {
        json j = json::parse(f);
        ApiKey          = j.value("ApiKey",           "");
        PollIntervalSec = j.value("PollIntervalSec",  30);
        ShowWindow      = j.value("ShowWindow",        true);
        ShowZeroDeltas  = j.value("ShowZeroDeltas",    false);
        TrackCurrency   = j.value("TrackCurrency",     true);
        TrackItems      = j.value("TrackItems",         true);
        AutoStart       = static_cast<AutoStartMode>(j.value("AutoStart", 0));
    }
    catch (...) { /* malformed json — ignore, use defaults */ }
}

void Settings::Save() const
{
    std::string path = SettingsPath();
    if (path.empty()) return;

    json j;
    j["ApiKey"]          = ApiKey;
    j["PollIntervalSec"] = PollIntervalSec;
    j["ShowWindow"]      = ShowWindow;
    j["ShowZeroDeltas"]  = ShowZeroDeltas;
    j["TrackCurrency"]   = TrackCurrency;
    j["TrackItems"]      = TrackItems;
    j["AutoStart"]       = static_cast<int>(AutoStart);

    std::ofstream f(path);
    if (f.is_open())
        f << j.dump(4);
}
