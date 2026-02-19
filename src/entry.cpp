#include "Shared.h"
#include "Settings.h"
#include "LootSession.h"
#include "GW2Api.h"
#include "UI.h"

#include <imgui.h>
#include <windows.h>
#include <string>

// ── DllMain ───────────────────────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID /*lpReserved*/)
{
    switch (ul_reason)
    {
        case DLL_PROCESS_ATTACH:
            Self = hModule;
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

// ── Keybind handler (plain C function pointer required by Nexus API) ──────────

static void ProcessKeybind(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    if (strcmp(aIdentifier, "KB_LOOTTRACKER_TOGGLEVIS") == 0)
    {
        g_Settings.ShowWindow = !g_Settings.ShowWindow;
        g_Settings.Save();
    }
}

// ── Addon lifecycle ───────────────────────────────────────────────────────────

static void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    // ── Set up ImGui to share the context Nexus is already running ────────────
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(aApi->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void*(*)(size_t, void*)>(aApi->ImguiMalloc),
        reinterpret_cast<void(*)(void*, void*)>(aApi->ImguiFree));

    // ── Grab shared Nexus data pointers ───────────────────────────────────────
    MumbleLink  = static_cast<Mumble::LinkedMem*>(
                      aApi->DataLink_Get(DL_MUMBLE_LINK));
    MumbleIdent = static_cast<Mumble::Identity*>(
                      aApi->DataLink_Get(DL_MUMBLE_LINK_IDENTITY));

    // ── Load persisted settings ────────────────────────────────────────────────
    // Settings path requires APIDefs to be non-null (already set above).
    g_Settings.Load();

    // ── Register render callbacks ─────────────────────────────────────────────
    aApi->GUI_Register(RT_Render,        UI::Render);
    aApi->GUI_Register(RT_OptionsRender, UI::RenderOptions);

    // ── Register keybind to toggle the window ─────────────────────────────────
    aApi->InputBinds_RegisterWithString(
        "KB_LOOTTRACKER_TOGGLEVIS",
        ProcessKeybind,
        "(null)"); // no default bind — user assigns in Nexus keybind settings

    // ── Add a quick-access shortcut ────────────────────────────────────────────
    // Texture identifiers below will resolve once Nexus loads the DLL icon.
    // Add them here anyway — Nexus will wait for the texture to be available.
    aApi->QuickAccess_Add(
        "QA_LOOTTRACKER",
        "ICON_LOOTTRACKER",
        "ICON_LOOTTRACKER_HOVER",
        "KB_LOOTTRACKER_TOGGLEVIS",
        "Loot Tracker");

    // Start polling thread and loot session.
    LootSession::Init();

    aApi->Log(LOGL_INFO, "LootTracker", "Loot Tracker loaded.");
}

static void AddonUnload()
{
    if (!APIDefs) return;

    // ── Stop background work first ────────────────────────────────────────────
    LootSession::Shutdown(); // calls GW2Api::StopPolling() internally

    // ── Deregister everything we registered ───────────────────────────────────
    APIDefs->GUI_Deregister(UI::Render);
    APIDefs->GUI_Deregister(UI::RenderOptions);
    APIDefs->InputBinds_Deregister("KB_LOOTTRACKER_TOGGLEVIS");
    APIDefs->QuickAccess_Remove("QA_LOOTTRACKER");

    // ── Persist final settings ────────────────────────────────────────────────
    g_Settings.Save();

    APIDefs->Log(LOGL_INFO, "LootTracker", "Loot Tracker unloaded.");

    APIDefs    = nullptr;
    MumbleLink = nullptr;
    MumbleIdent= nullptr;
}

// ── Addon definition — the only exported symbol Nexus needs ──────────────────

static AddonDefinition_t s_AddonDef{};

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    s_AddonDef.Signature   = 0xC0DE4C54; // Unique ID (unofficial — not on Raidcore)
    s_AddonDef.APIVersion  = NEXUS_API_VERSION;
    s_AddonDef.Name        = "Loot Tracker";
    s_AddonDef.Version     = { 1, 0, 0, 0 };
    s_AddonDef.Author      = "YoruDev-Ryland";
    s_AddonDef.Description = "Tracks items and currency gained per session, "
                             "like Blish HUD Session Tracker.";
    s_AddonDef.Load        = AddonLoad;
    s_AddonDef.Unload      = AddonUnload;
    s_AddonDef.Flags       = AF_None;
    s_AddonDef.Provider    = UP_GitHub;
    s_AddonDef.UpdateLink  = "https://github.com/YoruDev-Ryland/GW2-Nexus---Loot-Tracker";

    return &s_AddonDef;
}
