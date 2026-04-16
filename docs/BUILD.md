# Building kenshi_ai.dll

## Requirements

- Windows 10/11
- Visual Studio 2022 (MSVC v143, Desktop C++ workload)
- CMake 3.20+
- KenshiLib (fetched below)
- The game + RE_Kenshi installed (for testing)

## One-time setup

```powershell
cd native

# Fetch KenshiLib headers + prebuilt static lib
git clone -b RE_Kenshi_mods https://github.com/KenshiReclaimer/KenshiLib deps/KenshiLib

# Configure (x64 Release)
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release `
  -DKENSHI_MOD_OUTPUT_DIR="C:\path\to\Kenshi\mods\kenshi-ai"

# Build
cmake --build build --config Release
```

The DLL is copied to your mod folder automatically if `KENSHI_MOD_OUTPUT_DIR` is set.

## Mod folder layout after build

```
Kenshi/mods/kenshi-ai/
  kenshi-ai.json          RE_Kenshi plugin manifest
  kenshi-ai.mod           FCS mod registration
  kenshi_ai.dll           <-- built here
  kenshi_ai.ini           copied from config/kenshi_ai.ini.example + edited
  python/                 sidecar (run separately or configure as autostart)
```

## Running the sidecar

The DLL connects to the Python sidecar at `http://127.0.0.1:9392` by default.
Start it before launching Kenshi:

```bash
cd python
pip install -r requirements.txt
cp config/providers.json.example config/providers.json
# add your API key to providers.json
uvicorn server:app --host 127.0.0.1 --port 9392
```

## Debug build

```powershell
cmake -B build-debug -A x64 -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug
```

Attach Visual Studio debugger to `kenshi_x64.exe` after launch.
Output from `DebugLog()` appears in the RE_Kenshi console window.

## Notes

- Must be built as x64 — Kenshi is 64-bit only.
- MSVC runtime must match Kenshi's: `/MD` (MultiThreadedDLL).
- KenshiLib's `AddHook` uses Microsoft Detours under the hood; no extra dep needed.
- `startPlugin` export: MSVC mangles `void startPlugin()` to `?startPlugin@@YAXXZ`.
  If you rename the function or change the calling convention it won't load.
