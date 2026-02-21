> **Model Used: Claude Sonnet 4.6**

# Loot Tracker — GW2 Nexus Addon

A [Nexus](https://raidcore.gg/Nexus) addon for Guild Wars 2 that tracks items and currency gained per session — like the BlishHUD Session Tracker, but for Nexus.

---

## Features

- Tracks all **currency** (gold, karma, volatile magic, etc.) gained since the session snapshot was taken
- Tracks all **inventory items** gained or lost per session
- Optional display of zero-delta rows (items/currencies with no change)
- **Auto-start** modes — start tracking automatically on login or map load
- Item names, rarities, and vendor values resolved from the GW2 API and cached across restarts
- Persistent settings (API key, preferences) stored to disk (JSON)
- Nexus quick-access bar icon and keybind support

---

## Installation

1. Download `LootTracker.dll` from the [latest release](../../releases/latest).
2. Place it in your Nexus addons folder (typically `Guild Wars 2/addons/`).
3. Launch the game.
4. Load the addon from your Nexus library.

---

## Usage

1. Open the Loot Tracker window via the quick-access bar icon or your assigned keybind.
2. Go to **Options** and enter your GW2 API key. Required permissions: `account`, `wallet`, `inventories`.
3. Click **Start Session** (or enable auto-start in Options) to take a baseline snapshot.
4. Play the game — the tracker polls the API periodically and displays deltas.
5. Click **Reset** to take a new baseline at any point.

---

## Building from Source

### Prerequisites

- Visual Studio 2022 (MSVC) or Build Tools with the MSVC compiler
- CMake 3.20+
- Ninja (included with VS installer)
- Git

### Steps

```powershell
git clone https://github.com/YoruDev-Ryland/GW2-Nexus---Loot-Tracker.git
cd GW2-Nexus---Loot-Tracker

# Configure
cmake -B build -G "Ninja" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_CXX_COMPILER=cl `
  -DCMAKE_C_COMPILER=cl

# Build
cmake --build build --parallel

# Output: build/LootTracker.dll
```

CMake automatically fetches all dependencies (Nexus API header, ImGui v1.80, nlohmann/json) on first configure.

---

## Architecture

```
entry.cpp           DllMain + GetAddonDef + AddonLoad/Unload
Shared.h/.cpp       Global pointers: APIDefs, Self, MumbleLink, MumbleIdent
Settings.h/.cpp     Persistent settings (JSON) — API key, poll interval, etc.
GW2Api.h/.cpp       GW2 REST API calls + background polling thread
UI.h/.cpp           All ImGui rendering callbacks
```

### How session tracking works

1. On **Start Session**, a full `Snapshot` (wallet + inventory) is fetched from the GW2 API and stored as the baseline.
2. The background polling thread fetches a new snapshot every `PollIntervalSec` seconds.
3. The UI computes deltas between the latest snapshot and the baseline and displays them, grouped by currency and item.
4. Item display names, rarities, and vendor values are fetched from `/v2/items` in batches of up to 200 and cached in memory.

---

## Customising the Quick-Access Icon

Replace `src/icon.png` with your own 64×64 or 128×128 RGBA PNG, then rebuild.

---

## Contributing

Pull requests welcome.

---

## License

MIT