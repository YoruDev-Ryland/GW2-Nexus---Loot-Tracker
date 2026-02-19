#include "SessionHistory.h"
#include "Shared.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

using json = nlohmann::json;

// ── Internal state ─────────────────────────────────────────────────────────────
static std::mutex                     s_Mutex;
static std::vector<SessionHistory::SavedSession> s_Sessions;

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::string HistoryPath()
{
    if (!APIDefs || !APIDefs->Paths_GetAddonDirectory) return "";
    std::string dir = APIDefs->Paths_GetAddonDirectory("LootTracker");
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\history.json";
}

static std::string ToISO8601(std::chrono::system_clock::time_point tp)
{
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm utc{};
    gmtime_s(&utc, &t);
    std::ostringstream ss;
    ss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static void Persist()
{
    std::string path = HistoryPath();
    if (path.empty()) return;

    json arr = json::array();
    for (auto& s : s_Sessions)
    {
        json sess;
        sess["label"]          = s.label;
        sess["startTimestamp"] = s.startTimestamp;
        sess["endTimestamp"]   = s.endTimestamp;

        json items = json::array();
        for (auto& item : s.items)
        {
            json j;
            j["id"]          = item.id;
            j["name"]        = item.name;
            j["rarity"]      = item.rarity;
            j["delta"]       = item.delta;
            j["type"]        = item.type;
            j["description"] = item.description;
            j["vendorValue"] = item.vendorValue;
            items.push_back(std::move(j));
        }
        sess["items"] = std::move(items);

        json currencies = json::array();
        for (auto& c : s.currencies)
        {
            json j;
            j["id"]    = c.id;
            j["name"]  = c.name;
            j["delta"] = c.delta;
            currencies.push_back(std::move(j));
        }
        sess["currencies"] = std::move(currencies);

        arr.push_back(std::move(sess));
    }

    std::ofstream f(path);
    if (f.is_open()) f << arr.dump(2);
}

// ── Public API ─────────────────────────────────────────────────────────────────

void SessionHistory::Load()
{
    std::string path = HistoryPath();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) return;

    try
    {
        json arr = json::parse(f);
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Sessions.clear();

        for (auto& sess : arr)
        {
            SavedSession s;
            s.label          = sess.value("label",          "");
            s.startTimestamp = sess.value("startTimestamp", "");
            s.endTimestamp   = sess.value("endTimestamp",   "");

            for (auto& jitem : sess.value("items", json::array()))
            {
                LootSession::ItemDelta d;
                d.id          = jitem.value("id",          0);
                d.name        = jitem.value("name",        "");
                d.rarity      = jitem.value("rarity",      "");
                d.delta       = jitem.value("delta",       0);
                d.type        = jitem.value("type",        "");
                d.description = jitem.value("description", "");
                d.vendorValue = jitem.value("vendorValue", 0);
                s.items.push_back(std::move(d));
            }

            for (auto& jc : sess.value("currencies", json::array()))
            {
                LootSession::CurrencyDelta c;
                c.id    = jc.value("id",    0);
                c.name  = jc.value("name",  "");
                c.delta = jc.value("delta", (int64_t)0);
                s.currencies.push_back(std::move(c));
            }

            s_Sessions.push_back(std::move(s));
        }
    }
    catch (...) { /* malformed JSON — start fresh */ }
}

void SessionHistory::SaveSession(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::vector<LootSession::ItemDelta>     items,
    std::vector<LootSession::CurrencyDelta> currencies)
{
    // Only save if there's actually something to record
    bool hasContent = false;
    for (auto& i : items)      if (i.delta != 0)   { hasContent = true; break; }
    if (!hasContent)
        for (auto& c : currencies) if (c.delta != 0) { hasContent = true; break; }
    if (!hasContent) return;

    std::lock_guard<std::mutex> lock(s_Mutex);

    SavedSession s;
    s.label          = "Session " + std::to_string(s_Sessions.size() + 1);
    s.startTimestamp = ToISO8601(start);
    s.endTimestamp   = ToISO8601(end);
    s.items          = std::move(items);
    s.currencies     = std::move(currencies);

    s_Sessions.push_back(std::move(s));
    Persist();
}

std::vector<SessionHistory::SavedSession> SessionHistory::GetAll()
{
    std::lock_guard<std::mutex> lock(s_Mutex);
    auto copy = s_Sessions;
    std::reverse(copy.begin(), copy.end()); // newest first
    return copy;
}
