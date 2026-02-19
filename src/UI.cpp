#include "UI.h"
#include "Settings.h"
#include "LootSession.h"
#include "GW2Api.h"
#include "Shared.h"

#include <imgui.h>
#include <string>
#include <sstream>

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
            if (currencies.empty())
            {
                ImGui::TextDisabled("No currency changes yet.");
            }
            else
            {
                // Special handling for coin (id == 1) — display as gold breakdown
                for (auto& c : currencies)
                {
                    void* icon = GetTexResource(c.textureId);
                    if (icon)
                    {
                        ImGui::Image(icon, ImVec2(20, 20));
                        ImGui::SameLine();
                    }

                    ImVec4 col = c.delta >= 0
                        ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)  // green
                        : ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // red

                    ImGui::PushStyleColor(ImGuiCol_Text, col);

                    if (c.id == 1) // Gold coins
                    {
                        std::string sign = c.delta >= 0 ? "+" : "";
                        ImGui::Text("%s%s  %s",
                            sign.c_str(),
                            FormatGold(c.delta).c_str(),
                            c.name.c_str());
                    }
                    else
                    {
                        ImGui::Text("%+lld  %s", (long long)c.delta, c.name.c_str());
                    }

                    ImGui::PopStyleColor();
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

            if (items.empty())
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
                        if (!g_Settings.ShowZeroDeltas && item.delta == 0)
                            continue;

                        ImGui::TableNextRow();

                        // Icon column
                        ImGui::TableSetColumnIndex(0);
                        void* icon = GetTexResource(item.textureId);
                        if (icon)
                            ImGui::Image(icon, ImVec2(20, 20));
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

                        // Name column — coloured by rarity
                        ImGui::TableSetColumnIndex(2);
                        ImU32 rarityCol = RarityColor(item.rarity);
                        ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(rarityCol));
                        ImGui::TextUnformatted(item.name.c_str());
                        ImGui::PopStyleColor();

                        // Tooltip with chat link
                        if (!item.chatLink.empty() &&
                            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(item.chatLink.c_str());
                            ImGui::EndTooltip();
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

    // Poll interval slider
    ImGui::TextUnformatted("Poll interval (seconds)");
    if (ImGui::SliderInt("##PollInterval", &g_Settings.PollIntervalSec, 5, 120))
        g_Settings.Save();

    ImGui::Spacing();

    // Toggles
    if (ImGui::Checkbox("Track currency",      &g_Settings.TrackCurrency))   g_Settings.Save();
    if (ImGui::Checkbox("Track items",         &g_Settings.TrackItems))      g_Settings.Save();
    if (ImGui::Checkbox("Show zero deltas",    &g_Settings.ShowZeroDeltas))  g_Settings.Save();

    ImGui::Spacing();
    if (ImGui::Button("Open window"))
    {
        g_Settings.ShowWindow = true;
        g_Settings.Save();
    }
}
