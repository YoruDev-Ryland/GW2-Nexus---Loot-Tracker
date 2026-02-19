#pragma once
#include <windows.h>
#include <cstdint>
#include "Nexus.h"

// ── Mumble Link structs (standard GW2 memory layout) ─────────────────────────
// These match the GW2 wiki specification and what Nexus shares at DL_MUMBLE_LINK.
namespace Mumble
{
    struct Vector3 { float X, Y, Z; };

    struct Context
    {
        uint8_t  ServerAddress[28]; // sockaddr_in or sockaddr_in6
        uint32_t MapId;
        uint32_t MapType;
        uint32_t ShardId;
        uint32_t Instance;
        uint32_t BuildId;
        uint32_t UIState;           // bitfield: IsMapOpen, IsCompassTopRight, ...
        uint16_t CompassWidth;
        uint16_t CompassHeight;
        float    CompassRotation;
        float    PlayerX;
        float    PlayerY;
        float    MapCenterX;
        float    MapCenterY;
        float    MapScale;
        uint32_t ProcessId;
        uint8_t  MountIndex;
    };

    struct LinkedMem
    {
        uint32_t UIVersion;
        uint32_t UITick;
        Vector3  AvatarPosition;
        Vector3  AvatarFront;
        Vector3  AvatarTop;
        wchar_t  Name[256];         // L"Guild Wars 2" when in-game
        Vector3  CameraPosition;
        Vector3  CameraFront;
        Vector3  CameraTop;
        wchar_t  Identity[256];     // JSON: character name, map id, etc.
        uint32_t ContextLen;
        union {
            Context Context;
            uint8_t ContextRaw[256];
        };
        wchar_t  Description[2048];
    };

    // Parsed from LinkedMem::Identity JSON by Nexus — shared at DL_MUMBLE_LINK_IDENTITY
    struct Identity
    {
        char     Name[20];
        uint32_t Profession;
        uint32_t Spec;
        uint32_t Race;
        uint32_t MapID;
        uint32_t WorldID;
        uint32_t TeamColorID;
        bool     IsCommander;
        float    FOV;
        uint32_t UISize;
    };
}

// ── Global addon state shared across all translation units ───────────────────
extern AddonAPI_t*       APIDefs;      // Nexus API function table
extern HMODULE           Self;         // Our DLL's module handle

extern Mumble::LinkedMem* MumbleLink;    // Raw mumble data
extern Mumble::Identity*  MumbleIdent;   // Parsed identity (char name, map, etc.)
