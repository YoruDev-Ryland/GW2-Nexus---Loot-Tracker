#include "TrackingFilter.h"
#include "Shared.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <windows.h>

using json = nlohmann::json;

// ── Internal state ─────────────────────────────────────────────────────────────

static std::mutex                   s_Mutex;
static TrackingMode                 s_Mode   = TrackingMode::All;
static int                          s_Active = -1;  // index into s_Profiles; -1 = none
static std::vector<TrackingProfile> s_Profiles;

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::string ProfilesPath()
{
    if (!APIDefs || !APIDefs->Paths_GetAddonDirectory) return "";
    std::string dir = APIDefs->Paths_GetAddonDirectory("LootTracker");
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\profiles.json";
}

// ── Public API ─────────────────────────────────────────────────────────────────

TrackingMode TrackingFilter::GetMode()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Mode;
}

void TrackingFilter::SetMode(TrackingMode m)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Mode = m;
    if (m == TrackingMode::All) s_Active = -1;
}

int TrackingFilter::GetActiveProfileIndex()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Active;
}

void TrackingFilter::SetActiveProfile(int index)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    // Clamp or clear
    if (index < 0 || index >= (int)s_Profiles.size())
    {
        s_Active = -1;
        s_Mode   = TrackingMode::All;
    }
    else
    {
        s_Active = index;
        s_Mode   = TrackingMode::Custom;
    }
}

bool TrackingFilter::IsItemTracked(int id)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (s_Mode == TrackingMode::All || s_Active < 0) return true;
    if (s_Active >= (int)s_Profiles.size())           return true;
    auto& ids = s_Profiles[s_Active].itemIds;
    if (ids.empty()) return true;  // empty set = "track all items"
    return ids.count(id) > 0;
}

bool TrackingFilter::IsCurrencyTracked(int id)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (s_Mode == TrackingMode::All || s_Active < 0) return true;
    if (s_Active >= (int)s_Profiles.size())           return true;
    auto& ids = s_Profiles[s_Active].currencyIds;
    if (ids.empty()) return true; // empty set = "track all currencies"
    return ids.count(id) > 0;
}

std::vector<TrackingProfile> TrackingFilter::GetProfilesCopy()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    return s_Profiles;
}

int TrackingFilter::NewProfile(const std::string& name)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    TrackingProfile p;
    p.name = name;
    s_Profiles.push_back(std::move(p));
    int idx = (int)s_Profiles.size() - 1;
    s_Active = idx;
    s_Mode   = TrackingMode::Custom;
    return idx;
}

void TrackingFilter::DeleteProfile(int index)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (index < 0 || index >= (int)s_Profiles.size()) return;
    s_Profiles.erase(s_Profiles.begin() + index);

    // Fix active index
    if (s_Active == index)
        s_Active = -1;
    else if (s_Active > index)
        --s_Active;

    if (s_Active < 0 || s_Active >= (int)s_Profiles.size())
    {
        s_Active = -1;
        s_Mode   = TrackingMode::All;
    }
}

void TrackingFilter::UpdateProfile(int index, const TrackingProfile& p)
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    if (index < 0 || index >= (int)s_Profiles.size()) return;
    s_Profiles[index] = p;
}

// ── Persistence ────────────────────────────────────────────────────────────────

void TrackingFilter::Load()
{
    std::string path = ProfilesPath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) return;

    try
    {
        json j = json::parse(f);
        std::lock_guard<std::mutex> lock(s_Mutex);

        s_Active = j.value("active", -1);
        s_Mode   = static_cast<TrackingMode>(j.value("mode", 0));
        s_Profiles.clear();

        for (auto& jp : j.value("profiles", json::array()))
        {
            TrackingProfile p;
            p.name = jp.value("name", "");
            for (int id : jp.value("itemIds",     json::array())) p.itemIds.insert(id);
            for (int id : jp.value("currencyIds", json::array())) p.currencyIds.insert(id);
            s_Profiles.push_back(std::move(p));
        }

        // Guard against stale active index
        if (s_Active >= (int)s_Profiles.size())
        { s_Active = -1; s_Mode = TrackingMode::All; }
    }
    catch (...) {}
}

void TrackingFilter::Save()
{
    std::string path = ProfilesPath();
    if (path.empty()) return;

    std::lock_guard<std::mutex> lock(s_Mutex);

    json j;
    j["active"] = s_Active;
    j["mode"]   = static_cast<int>(s_Mode);

    json jprofiles = json::array();
    for (auto& p : s_Profiles)
    {
        json jp;
        jp["name"] = p.name;
        jp["itemIds"]     = json::array();
        jp["currencyIds"] = json::array();
        for (int id : p.itemIds)     jp["itemIds"].push_back(id);
        for (int id : p.currencyIds) jp["currencyIds"].push_back(id);
        jprofiles.push_back(std::move(jp));
    }
    j["profiles"] = std::move(jprofiles);

    std::ofstream f(path);
    if (f.is_open()) f << j.dump(2);
}
