#include "UI.h"
#include "Settings.h"
#include "LootSession.h"
#include "GW2Api.h"
#include "Shared.h"
#include "SessionHistory.h"
#include "TrackingFilter.h"

#include <imgui.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

// ── Helpers ───────────────────────────────────────────────────────────────────

// GW2 rarity colour palette using IM_COL32(R, G, B, A).
static ImU32 RarityColor(const std::string& rarity)
{
    if (rarity == "Junk")       return IM_COL32(170, 170, 170, 255); // grey
    if (rarity == "Basic")      return IM_COL32(255, 255, 255, 255); // white
    if (rarity == "Fine")       return IM_COL32(102, 153, 255, 255); // blue
    if (rarity == "Masterwork") return IM_COL32( 26, 147,   6, 255); // green
    if (rarity == "Rare")       return IM_COL32(250, 183,   0, 255); // gold
    if (rarity == "Exotic")     return IM_COL32(200,  96,  10, 255); // orange
    if (rarity == "Ascended")   return IM_COL32(251,  62, 141, 255); // pink
    if (rarity == "Legendary")  return IM_COL32( 76,  19, 157, 255); // purple
    return IM_COL32(255, 255, 255, 255);
}

// Format a coin value (e.g. 123456 -> "12g 34s 56c").
static std::string FormatGold(int64_t coins)
{
    bool negative = coins < 0;
    int64_t abs_coins = negative ? -coins : coins;

    int64_t gold   = abs_coins / 10000;
    int64_t silver = (abs_coins % 10000) / 100;
    int64_t copper = abs_coins % 100;

    std::ostringstream ss;
    if (negative) ss << "-";
    if (gold)   ss << gold   << "g ";
    if (silver) ss << silver << "s ";
    ss << copper << "c";
    return ss.str();
}

// Format elapsed seconds as HH:MM:SS.
static std::string FormatDuration(std::chrono::seconds dur)
{
    auto h = dur.count() / 3600;
    auto m = (dur.count() % 3600) / 60;
    auto s = dur.count() % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
             (long long)h, (long long)m, (long long)s);
    return buf;
}

// Try to get a texture's ID3D11ShaderResourceView* via Nexus — returns nullptr
// if not loaded yet (icon will show as a coloured placeholder).
static void* GetTexResource(const std::string& texId)
{
    if (!APIDefs || texId.empty()) return nullptr;
    Texture_t* t = APIDefs->Textures_Get(texId.c_str());
    return (t && t->Resource) ? t->Resource : nullptr;
}

// ── Main window ───────────────────────────────────────────────────────────────

static bool s_ShowHistory = false;

// ── Profile editor state ───────────────────────────────────────────────────────
static bool            s_ShowProfileEditor    = false;
static bool            s_ConfirmDeleteProfile = false;
static int             s_EditingProfileIdx    = -1;   // -1 = creating new
static TrackingProfile s_WorkingProfile;
static char            s_ProfileNameBuf[64] = {};

void UI::Render()
{
    if (!g_Settings.ShowWindow) return;

    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 200), ImVec2(800, 1200));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Loot Tracker##LT_Main", &g_Settings.ShowWindow, flags))
    {
        ImGui::End();
        return;
    }

    // ── Header row: timer + controls ─────────────────────────────────────────
    bool active = LootSession::IsActive();

    ImGui::TextUnformatted(active
        ? ("Session: " + FormatDuration(LootSession::ElapsedTime())).c_str()
        : "Session: stopped");

    ImGui::SameLine();

    if (active)
    {
        if (ImGui::SmallButton("Stop"))
            LootSession::Stop();
    }
    else
    {
        if (ImGui::SmallButton("Start"))
        {
            LootSession::Start();
            GW2Api::PollNow();
        }
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
    {
        LootSession::Stop();
        LootSession::Start();
        GW2Api::PollNow();
    }

    ImGui::Separator();

    // ── Profile bar ───────────────────────────────────────────────────────────
    {
        auto profiles  = TrackingFilter::GetProfilesCopy();
        int  activeIdx = TrackingFilter::GetActiveProfileIndex();
        const char* activeLabel = (activeIdx < 0 || activeIdx >= (int)profiles.size())
            ? "All" : profiles[activeIdx].name.c_str();

        ImGui::TextUnformatted("Profile:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(145.0f);
        if (ImGui::BeginCombo("##LTProfSel", activeLabel))
        {
            if (ImGui::Selectable("All##LTProfAll", activeIdx < 0))
            { TrackingFilter::SetActiveProfile(-1); TrackingFilter::Save(); }
            if (activeIdx < 0) ImGui::SetItemDefaultFocus();

            for (int i = 0; i < (int)profiles.size(); i++)
            {
                bool sel = (i == activeIdx);
                if (ImGui::Selectable(profiles[i].name.c_str(), sel))
                { TrackingFilter::SetActiveProfile(i); TrackingFilter::Save(); }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
        {
            s_WorkingProfile       = {};
            s_EditingProfileIdx    = -1;
            s_ConfirmDeleteProfile = false;
            memset(s_ProfileNameBuf, 0, sizeof(s_ProfileNameBuf));
            snprintf(s_ProfileNameBuf, sizeof(s_ProfileNameBuf), "New Profile");
            s_ShowProfileEditor = true;
        }
        if (ImGui::IsItemHovered())
        { ImGui::BeginTooltip(); ImGui::TextUnformatted("New profile"); ImGui::EndTooltip(); }

        if (activeIdx >= 0 && activeIdx < (int)profiles.size())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
            {
                s_WorkingProfile       = profiles[activeIdx];
                s_EditingProfileIdx    = activeIdx;
                s_ConfirmDeleteProfile = false;
                strncpy_s(s_ProfileNameBuf, sizeof(s_ProfileNameBuf),
                    s_WorkingProfile.name.c_str(), _TRUNCATE);
                s_ShowProfileEditor = true;
            }
        }
    }
    ImGui::Separator();

    // ── API key warning ───────────────────────────────────────────────────────
    if (g_Settings.ApiKey.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
        ImGui::TextWrapped("No API key set. Open Nexus Options > Loot Tracker to configure.");
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }

    // ── Currency section ──────────────────────────────────────────────────────
    if (g_Settings.TrackCurrency)
    {
        if (ImGui::CollapsingHeader("Currency", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto currencies = LootSession::GetCurrencyDeltas();

            // When a profile is active, inject zero-delta placeholders for
            // tracked currencies not yet seen this session.
            if (TrackingFilter::GetMode() == TrackingMode::Custom)
            {
                int activeIdx = TrackingFilter::GetActiveProfileIndex();
                auto profiles = TrackingFilter::GetProfilesCopy();
                if (activeIdx >= 0 && activeIdx < (int)profiles.size())
                {
                    std::unordered_set<int> present;
                    for (auto& c : currencies) present.insert(c.id);

                    auto knownCurs = LootSession::GetKnownCurrencies();
                    std::unordered_map<int, LootSession::KnownCurrency> knownMap;
                    for (auto& kc : knownCurs) knownMap[kc.id] = kc;

                    for (int id : profiles[activeIdx].currencyIds)
                    {
                        if (present.count(id)) continue;
                        LootSession::CurrencyDelta d;
                        d.id        = id;
                        d.delta     = 0;
                        d.textureId = "LT_CURRENCY_" + std::to_string(id);
                        auto it = knownMap.find(id);
                        d.name = (it != knownMap.end()) ? it->second.name
                                                        : "Currency #" + std::to_string(id);
                        currencies.push_back(std::move(d));
                    }
                }
            }

            // Check whether anything will actually render
            bool anyVisible = false;
            for (auto& c : currencies)
                if (TrackingFilter::IsCurrencyTracked(c.id)) { anyVisible = true; break; }

            if (!anyVisible)
            {
                ImGui::TextDisabled("No currency changes yet.");
            }
            else
            {
                // Display each currency row
                for (auto& c : currencies)
                {
                    if (!TrackingFilter::IsCurrencyTracked(c.id)) continue;

                    void* icon = GetTexResource(c.textureId);
                    if (icon)
                    {
                        ImGui::Image((ImTextureID)icon, ImVec2(20, 20));
                        ImGui::SameLine();
                    }

                    ImVec4 col = c.delta >= 0
                        ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)  // green
                        : ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // red

                    // Selectable (instead of Text) so right-click popup attaches cleanly
                    std::string dispText;
                    if (c.id == 1)
                        dispText = (c.delta >= 0 ? "+" : "") + FormatGold(c.delta) + "  " + c.name;
                    else
                    {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "%+lld  %s", (long long)c.delta, c.name.c_str());
                        dispText = buf;
                    }
                    std::string curSel = dispText + "##cur" + std::to_string(c.id);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Selectable(curSel.c_str(), false, 0, ImVec2(0, 22));
                    ImGui::PopStyleColor();

                    // Right-click: add / remove from profile
                    char curCtxId[32];
                    snprintf(curCtxId, sizeof(curCtxId), "LTCurRC%d", c.id);
                    if (ImGui::BeginPopupContextItem(curCtxId))
                    {
                        auto profiles  = TrackingFilter::GetProfilesCopy();
                        int  activeIdx = TrackingFilter::GetActiveProfileIndex();
                        ImGui::TextDisabled("%s", c.name.c_str());
                        ImGui::Separator();
                        if (!profiles.empty())
                        {
                            if (ImGui::BeginMenu("Add to profile"))
                            {
                                for (int pi = 0; pi < (int)profiles.size(); pi++)
                                {
                                    bool already = profiles[pi].currencyIds.count(c.id) > 0;
                                    if (ImGui::MenuItem(profiles[pi].name.c_str(), nullptr, already))
                                    {
                                        auto p = profiles[pi];
                                        if (already) p.currencyIds.erase(c.id);
                                        else         p.currencyIds.insert(c.id);
                                        TrackingFilter::UpdateProfile(pi, p);
                                        TrackingFilter::Save();
                                    }
                                }
                                ImGui::EndMenu();
                            }
                        }
                        else
                        {
                            if (ImGui::MenuItem("Create first profile to track..."))
                            {
                                s_WorkingProfile = {};
                                s_WorkingProfile.currencyIds.insert(c.id);
                                s_EditingProfileIdx = -1;
                                memset(s_ProfileNameBuf, 0, sizeof(s_ProfileNameBuf));
                                snprintf(s_ProfileNameBuf, sizeof(s_ProfileNameBuf), "New Profile");
                                s_ShowProfileEditor = true;
                            }
                        }
                        ImGui::EndPopup();
                    }
                }
            }
        }
    }

    // ── Items section ─────────────────────────────────────────────────────────
    if (g_Settings.TrackItems)
    {
        if (ImGui::CollapsingHeader("Items", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto items = LootSession::GetItemDeltas();

            // When a profile is active, inject zero-delta placeholders for
            // tracked items not yet seen this session.
            if (TrackingFilter::GetMode() == TrackingMode::Custom)
            {
                int activeIdx = TrackingFilter::GetActiveProfileIndex();
                auto profiles = TrackingFilter::GetProfilesCopy();
                if (activeIdx >= 0 && activeIdx < (int)profiles.size())
                {
                    std::unordered_set<int> present;
                    for (auto& d : items) present.insert(d.id);

                    auto knownItems = LootSession::GetKnownItems();
                    std::unordered_map<int, LootSession::KnownItem> knownMap;
                    for (auto& ki : knownItems) knownMap[ki.id] = ki;

                    for (int id : profiles[activeIdx].itemIds)
                    {
                        if (present.count(id)) continue;
                        LootSession::ItemDelta d;
                        d.id        = id;
                        d.delta     = 0;
                        d.textureId = "LT_ITEM_" + std::to_string(id);
                        auto it = knownMap.find(id);
                        if (it != knownMap.end())
                        {
                            d.name   = it->second.name;
                            d.rarity = it->second.rarity;
                            d.type   = it->second.type;
                        }
                        else
                        {
                            d.name = "Item #" + std::to_string(id);
                        }
                        items.push_back(std::move(d));
                    }
                }
            }

            // Check whether anything will render (respecting filter + zero setting)
            bool anyItemVisible = false;
            for (auto& item : items)
            {
                if (!TrackingFilter::IsItemTracked(item.id)) continue;
                // Profile-pinned items always show even at delta 0
                bool pinned = (TrackingFilter::GetMode() == TrackingMode::Custom &&
                               TrackingFilter::IsItemTracked(item.id));
                if (!g_Settings.ShowZeroDeltas && item.delta == 0 && !pinned) continue;
                anyItemVisible = true; break;
            }

            if (!anyItemVisible)
            {
                ImGui::TextDisabled("No item changes yet.");
            }
            else
            {
                // Table with icon | count | name columns
                if (ImGui::BeginTable("LT_Items", 3,
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_RowBg   |
                    ImGuiTableFlags_BordersInnerV,
                    ImVec2(0, 0)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed,  24.0f);
                    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed,  50.0f);
                    ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (auto& item : items)
                    {
                        if (!TrackingFilter::IsItemTracked(item.id)) continue;

                        // Profile-pinned items always show, even at delta 0
                        bool pinned = (TrackingFilter::GetMode() == TrackingMode::Custom);
                        if (!g_Settings.ShowZeroDeltas && item.delta == 0 && !pinned)
                            continue;

                        ImGui::TableNextRow();

                        // Icon column
                        ImGui::TableSetColumnIndex(0);
                        void* icon = GetTexResource(item.textureId);
                        if (icon)
                            ImGui::Image((ImTextureID)icon, ImVec2(20, 20));
                        else
                        {
                            // Colour placeholder square
                            ImU32 col = RarityColor(item.rarity);
                            ImGui::ColorButton("##sq",
                                ImGui::ColorConvertU32ToFloat4(col),
                                ImGuiColorEditFlags_NoTooltip |
                                ImGuiColorEditFlags_NoBorder,
                                ImVec2(20, 20));
                        }

                        // Count column
                        ImGui::TableSetColumnIndex(1);
                        ImVec4 countCol = item.delta >= 0
                            ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                            : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, countCol);
                        ImGui::Text("%+d", item.delta);
                        ImGui::PopStyleColor();

                        // Name column — coloured by rarity, Selectable for right-click
                        ImGui::TableSetColumnIndex(2);
                        ImU32 rarityCol = RarityColor(item.rarity);
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(rarityCol));
                        std::string itmSel = item.name + "##itm" + std::to_string(item.id);
                        ImGui::Selectable(itmSel.c_str(), false, 0, ImVec2(0, 20));
                        ImGui::PopStyleColor();

                        // Tooltip with item details (hover over name)
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            if (!item.type.empty() || !item.rarity.empty())
                            {
                                std::string hdr;
                                if (!item.rarity.empty()) hdr = item.rarity;
                                if (!item.rarity.empty() && !item.type.empty()) hdr += " ";
                                if (!item.type.empty())   hdr += item.type;
                                ImGui::TextDisabled("%s", hdr.c_str());
                            }
                            if (!item.description.empty())
                            {
                                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 16.0f);
                                ImGui::TextUnformatted(item.description.c_str());
                                ImGui::PopTextWrapPos();
                            }
                            if (item.vendorValue > 0)
                            {
                                ImGui::Separator();
                                ImGui::Text("Vendor: %s", FormatGold(item.vendorValue).c_str());
                            }
                            ImGui::EndTooltip();
                        }

                        // Right-click: add / remove from profile
                        char itmCtxId[32];
                        snprintf(itmCtxId, sizeof(itmCtxId), "LTItmRC%d", item.id);
                        if (ImGui::BeginPopupContextItem(itmCtxId))
                        {
                            auto profiles  = TrackingFilter::GetProfilesCopy();
                            int  activeIdx = TrackingFilter::GetActiveProfileIndex();
                            ImGui::TextDisabled("%s", item.name.c_str());
                            ImGui::Separator();
                            if (!profiles.empty())
                            {
                                if (ImGui::BeginMenu("Add to profile"))
                                {
                                    for (int pi = 0; pi < (int)profiles.size(); pi++)
                                    {
                                        bool already = profiles[pi].itemIds.count(item.id) > 0;
                                        if (ImGui::MenuItem(profiles[pi].name.c_str(), nullptr, already))
                                        {
                                            auto p = profiles[pi];
                                            if (already) p.itemIds.erase(item.id);
                                            else         p.itemIds.insert(item.id);
                                            TrackingFilter::UpdateProfile(pi, p);
                                            TrackingFilter::Save();
                                        }
                                    }
                                    ImGui::EndMenu();
                                }
                            }
                            else
                            {
                                if (ImGui::MenuItem("Create first profile to track..."))
                                {
                                    s_WorkingProfile = {};
                                    s_WorkingProfile.itemIds.insert(item.id);
                                    s_EditingProfileIdx = -1;
                                    memset(s_ProfileNameBuf, 0, sizeof(s_ProfileNameBuf));
                                    snprintf(s_ProfileNameBuf, sizeof(s_ProfileNameBuf), "New Profile");
                                    s_ShowProfileEditor = true;
                                }
                            }
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }
    }

    ImGui::End();
}

// ── Options panel (shown in Nexus Options window) ─────────────────────────────

void UI::RenderOptions()
{
    ImGui::TextUnformatted("Loot Tracker");
    ImGui::Separator();

    // API key input
    ImGui::TextUnformatted("GW2 API Key");
    ImGui::SetNextItemWidth(-1.0f);

    static char s_ApiKeyBuf[73] = {}; // GW2 keys are 72 chars + null
    static bool s_Initialised   = false;
    if (!s_Initialised)
    {
        strncpy_s(s_ApiKeyBuf, g_Settings.ApiKey.c_str(), 72);
        s_Initialised = true;
    }

    ImGuiInputTextFlags keyFlags =
        ImGuiInputTextFlags_Password |
        ImGuiInputTextFlags_EnterReturnsTrue;

    if (ImGui::InputText("##APIKey", s_ApiKeyBuf, sizeof(s_ApiKeyBuf), keyFlags))
    {
        g_Settings.ApiKey = s_ApiKeyBuf;
        g_Settings.Save();
        GW2Api::PollNow(); // validate + fetch immediately
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply"))
    {
        g_Settings.ApiKey = s_ApiKeyBuf;
        g_Settings.Save();
        GW2Api::PollNow();
    }
    ImGui::TextDisabled("Key needs: inventories + wallet permissions");

    ImGui::Spacing();

    // Toggles
    if (ImGui::Checkbox("Track currency",      &g_Settings.TrackCurrency))   g_Settings.Save();
    if (ImGui::Checkbox("Track items",         &g_Settings.TrackItems))      g_Settings.Save();
    if (ImGui::Checkbox("Show zero deltas",    &g_Settings.ShowZeroDeltas))  g_Settings.Save();

    ImGui::Spacing();

    // Auto-start mode
    ImGui::TextUnformatted("Auto-start new session");
    static const char* s_AutoStartLabels[] = {
        "Disabled",
        "Every login",
        "Every hour (UTC)",
        "Daily reset (00:00 UTC)",
    };
    int current = static_cast<int>(g_Settings.AutoStart);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##AutoStart", &current, s_AutoStartLabels, 4))
    {
        g_Settings.AutoStart = static_cast<AutoStartMode>(current);
        g_Settings.Save();
    }

    ImGui::Spacing();
    if (ImGui::Button("Open window"))
    {
        g_Settings.ShowWindow = true;
        g_Settings.Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("View History"))
        s_ShowHistory = true;
}

// ── History window ─────────────────────────────────────────────────────────────

void UI::RenderHistory()
{
    if (!s_ShowHistory) return;

    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Loot Tracker \u2013 History", &s_ShowHistory,
            ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    auto sessions = SessionHistory::GetAll();
    if (sessions.empty())
    {
        ImGui::TextDisabled("No completed sessions yet.");
        ImGui::End();
        return;
    }

    for (size_t si = 0; si < sessions.size(); ++si)
    {
        auto& sess = sessions[si];
        std::string header = sess.label + "  [" + sess.startTimestamp
                             + " – " + sess.endTimestamp + "]";

        if (ImGui::CollapsingHeader(header.c_str()))
        {
            // Currency sub-section
            if (!sess.currencies.empty())
            {
                ImGui::TextDisabled("Currency");
                for (auto& c : sess.currencies)
                {
                    ImVec4 col = c.delta >= 0
                        ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                        : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    if (c.id == 1)
                    {
                        std::string sign = c.delta >= 0 ? "+" : "";
                        ImGui::Text("  %s%s  %s",
                            sign.c_str(),
                            FormatGold(c.delta).c_str(),
                            c.name.c_str());
                    }
                    else
                        ImGui::Text("  %+lld  %s", (long long)c.delta, c.name.c_str());
                    ImGui::PopStyleColor();
                }
            }

            // Items sub-section
            if (!sess.items.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Items");
                if (ImGui::BeginTable(("LT_Hist_" + std::to_string(si)).c_str(), 3,
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_RowBg   |
                    ImGuiTableFlags_BordersInnerV,
                    ImVec2(0, std::min((int)sess.items.size(), 10) * 22.0f + 22.0f)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed,  24.0f);
                    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed,  50.0f);
                    ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (auto& item : sess.items)
                    {
                        if (!g_Settings.ShowZeroDeltas && item.delta == 0) continue;
                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        void* icon = GetTexResource(item.textureId);
                        if (icon)
                            ImGui::Image((ImTextureID)icon, ImVec2(20, 20));

                        ImGui::TableSetColumnIndex(1);
                        ImVec4 cc = item.delta >= 0
                            ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                            : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, cc);
                        ImGui::Text("%+d", item.delta);
                        ImGui::PopStyleColor();

                        ImGui::TableSetColumnIndex(2);
                        ImU32 rc = RarityColor(item.rarity);
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(rc));
                        ImGui::TextUnformatted(item.name.c_str());
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndTable();
                }
            }
        }
    }

    ImGui::End();
}

// ── Profile Editor window ─────────────────────────────────────────────────────

void UI::RenderProfileEditor()
{
    if (!s_ShowProfileEditor) return;

    ImGui::SetNextWindowSize(ImVec2(400, 520), ImGuiCond_FirstUseEver);
    bool open = s_ShowProfileEditor;
    if (!ImGui::Begin("Profile Editor##LT_PE", &open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        s_ShowProfileEditor = open;
        return;
    }
    s_ShowProfileEditor = open;

    // Profile name
    ImGui::TextUnformatted("Profile Name:");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##PEName", s_ProfileNameBuf, sizeof(s_ProfileNameBuf));
    ImGui::Spacing();

    // Content area — leave room for Save/Cancel buttons at bottom
    ImGui::BeginChild("##PEContent", ImVec2(0, -76), false);

    if (ImGui::BeginTabBar("##PETabs"))
    {
        // ── Currencies tab ────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Currencies"))
        {
            ImGui::TextDisabled("Click a currency to toggle tracking in this profile.");
            ImGui::Spacing();

            auto curs = LootSession::GetKnownCurrencies();
            if (curs.empty())
                ImGui::TextDisabled("No currencies seen yet — a poll will populate this list.");

            std::sort(curs.begin(), curs.end(),
                [](const LootSession::KnownCurrency& a, const LootSession::KnownCurrency& b)
                { return a.name < b.name; });

            for (auto& c : curs)
            {
                bool tracked = s_WorkingProfile.currencyIds.count(c.id) > 0;

                void* icon = GetTexResource(c.textureId);
                if (icon) { ImGui::Image((ImTextureID)icon, ImVec2(20, 20)); ImGui::SameLine(); }

                ImGui::PushStyleColor(ImGuiCol_Text,
                    tracked ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1, 1, 1, 0.9f));
                std::string selLabel = c.name + "##ce_" + std::to_string(c.id);
                if (ImGui::Selectable(selLabel.c_str(), tracked, 0, ImVec2(0, 22)))
                {
                    if (tracked) s_WorkingProfile.currencyIds.erase(c.id);
                    else         s_WorkingProfile.currencyIds.insert(c.id);
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndTabItem();
        }

        // ── Items tab ─────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Items"))
        {
            static char s_PESearch[64] = {};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("Search##PESearch", s_PESearch, sizeof(s_PESearch));
            ImGui::Spacing();

            // Build lookup map of all known items
            auto knownItems = LootSession::GetKnownItems();
            std::unordered_map<int, const LootSession::KnownItem*> knownMap;
            for (auto& ki : knownItems) knownMap[ki.id] = &ki;

            std::string searchLow = s_PESearch;
            for (auto& ch : searchLow) ch = (char)std::tolower((unsigned char)ch);

            // Section 1: items currently tracked in this profile
            if (!s_WorkingProfile.itemIds.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 0.5f, 1.0f));
                ImGui::TextUnformatted("Tracked in this profile");
                ImGui::PopStyleColor();
                ImGui::Separator();

                std::vector<std::pair<int,std::string>> tracked;
                for (int id : s_WorkingProfile.itemIds)
                {
                    auto it = knownMap.find(id);
                    std::string nm = (it != knownMap.end())
                        ? it->second->name : "Item #" + std::to_string(id);
                    if (!searchLow.empty())
                    {
                        std::string nl = nm;
                        for (auto& ch : nl) ch = (char)std::tolower((unsigned char)ch);
                        if (nl.find(searchLow) == std::string::npos) continue;
                    }
                    tracked.push_back({id, nm});
                }
                std::sort(tracked.begin(), tracked.end(),
                    [](auto& a, auto& b){ return a.second < b.second; });

                for (auto& [id, nm] : tracked)
                {
                    auto it = knownMap.find(id);
                    std::string rarity, texId;
                    if (it != knownMap.end())
                    {
                        rarity = it->second->rarity;
                        texId  = it->second->textureId;
                    }

                    void* icon = texId.empty() ? nullptr : GetTexResource(texId);
                    if (icon) { ImGui::Image((ImTextureID)icon, ImVec2(20, 20)); ImGui::SameLine(); }
                    else if (!rarity.empty())
                    {
                        ImGui::ColorButton("##pe_t",
                            ImGui::ColorConvertU32ToFloat4(RarityColor(rarity)),
                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                            ImVec2(20, 20));
                        ImGui::SameLine();
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                    std::string lbl = nm + "##it_" + std::to_string(id);
                    if (ImGui::Selectable(lbl.c_str(), true, 0, ImVec2(0, 22)))
                        s_WorkingProfile.itemIds.erase(id);
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                    { ImGui::BeginTooltip(); ImGui::TextUnformatted("Click to remove from profile"); ImGui::EndTooltip(); }
                }
                ImGui::Spacing();
            }

            // Section 2: available items (seen but not yet tracked)
            {
                std::vector<const LootSession::KnownItem*> avail;
                for (auto& ki : knownItems)
                {
                    if (s_WorkingProfile.itemIds.count(ki.id)) continue;
                    if (!searchLow.empty())
                    {
                        std::string nl = ki.name;
                        for (auto& ch : nl) ch = (char)std::tolower((unsigned char)ch);
                        if (nl.find(searchLow) == std::string::npos) continue;
                    }
                    avail.push_back(&ki);
                }
                if (!avail.empty())
                {
                    std::sort(avail.begin(), avail.end(),
                        [](const LootSession::KnownItem* a, const LootSession::KnownItem* b)
                        { return a->type == b->type ? a->name < b->name : a->type < b->type; });
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::TextUnformatted("Available items (seen this account)");
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                    std::string lastType;
                    for (auto* item : avail)
                    {
                        if (item->type != lastType)
                        {
                            lastType = item->type;
                            ImGui::Spacing();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
                            ImGui::TextUnformatted(lastType.empty() ? "Unknown" : lastType.c_str());
                            ImGui::PopStyleColor();
                        }
                        void* icon = GetTexResource(item->textureId);
                        if (icon) { ImGui::Image((ImTextureID)icon, ImVec2(20, 20)); ImGui::SameLine(); }
                        else
                        {
                            ImGui::ColorButton("##pe_sq",
                                ImGui::ColorConvertU32ToFloat4(RarityColor(item->rarity)),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                                ImVec2(20, 20));
                            ImGui::SameLine();
                        }
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.85f));
                        std::string lbl = item->name + "##ie_" + std::to_string(item->id);
                        if (ImGui::Selectable(lbl.c_str(), false, 0, ImVec2(0, 22)))
                            s_WorkingProfile.itemIds.insert(item->id);
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                        { ImGui::BeginTooltip(); ImGui::TextUnformatted("Click to add to profile"); ImGui::EndTooltip(); }
                    }
                }
                else if (s_WorkingProfile.itemIds.empty())
                {
                    ImGui::TextDisabled("No items seen yet. Play a session to populate this list.");
                }
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild(); // ##PEContent

    // ── Buttons ───────────────────────────────────────────────────────────────
    ImGui::Separator();

    if (!s_ConfirmDeleteProfile)
    {
        if (ImGui::Button("Save"))
        {
            s_WorkingProfile.name = s_ProfileNameBuf;
            if (s_EditingProfileIdx >= 0)
                TrackingFilter::UpdateProfile(s_EditingProfileIdx, s_WorkingProfile);
            else
            {
                int idx = TrackingFilter::NewProfile(s_WorkingProfile.name);
                TrackingFilter::UpdateProfile(idx, s_WorkingProfile);
            }
            TrackingFilter::Save();
            s_ShowProfileEditor = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            s_ShowProfileEditor = false;

        if (s_EditingProfileIdx >= 0)
        {
            // Right-aligned red Delete button
            float btnW = ImGui::CalcTextSize("Delete...").x
                         + ImGui::GetStyle().FramePadding.x * 2.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            float moveX = avail - btnW;
            if (moveX > ImGui::GetStyle().ItemSpacing.x)
                ImGui::SameLine(0, moveX);
            else
                ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Delete..."))
                s_ConfirmDeleteProfile = true;
            ImGui::PopStyleColor(2);
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextUnformatted("This cannot be undone.");
        ImGui::PopStyleColor();
        ImGui::Text("Delete profile \"%.40s\"?", s_ProfileNameBuf);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Yes, Delete"))
        {
            TrackingFilter::DeleteProfile(s_EditingProfileIdx);
            TrackingFilter::Save();
            s_ShowProfileEditor    = false;
            s_ConfirmDeleteProfile = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Nevermind"))
            s_ConfirmDeleteProfile = false;
    }

    ImGui::End();
}
