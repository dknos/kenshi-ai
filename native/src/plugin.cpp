/**
 * plugin.cpp — RE_Kenshi plugin entry point.
 *
 * RE_Kenshi calls GetProcAddress(hDll, "?startPlugin@@YAXXZ") after loading
 * the DLL. That mangled name resolves to void startPlugin() with __cdecl
 * calling convention and no arguments.
 *
 * We hook dialogue functions here and load config from the mod folder.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>

#include <kenshi/Globals.h>        // ou, con, key
#include <kenshi/GameWorld.h>
#include <kenshi/ModInfo.h>
#include <core/Functions.h>        // KenshiLib::AddHook, GetRealAddress

#include "kenshi_ai.h"

// Defined in hooks.cpp
namespace Hooks
{
    void Init();
}

// RE_Kenshi reads this exact mangled export to start the plugin.
extern "C" __declspec(dllexport) void startPlugin()
{
    // Find this mod's folder so we can load kenshi_ai.ini and config/
    std::string modFolder;
    lektor<ModInfo*>& mods = ou->activeMods;
    for (int i = 0; i < mods.size(); ++i)
    {
        // Our mod has RE_Kenshi.json with "Plugins": ["kenshi_ai.dll"]
        std::string candidate = mods[i]->path + "\\kenshi_ai.dll";
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            modFolder = mods[i]->path;
            break;
        }
    }

    KenshiAI::LoadConfig(modFolder);
    Hooks::Init();
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    // Nothing to do here — RE_Kenshi loads us after the engine is ready.
    return TRUE;
}
