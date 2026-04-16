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

// Captured in DllMain so startPlugin doesn't need to scan activeMods.
static std::string g_modFolder;

// RE_Kenshi calls GetProcAddress(hDll, "?startPlugin@@YAXXZ").
// Do NOT use extern "C" here — that would export as plain "startPlugin".
__declspec(dllexport) void startPlugin()
{
    if (FILE* f = fopen("C:\\Users\\Public\\kenshi_ai_start.txt", "w"))
    {
        fprintf(f, "startPlugin called\nmodFolder=%s\n", g_modFolder.c_str());
        fclose(f);
    }

    KenshiAI::LoadConfig(g_modFolder);
    Hooks::Init();
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(hInst, path, MAX_PATH);
        // Strip "kenshi_ai.dll" to get the mod folder.
        std::string p(path);
        auto slash = p.find_last_of("\\/");
        g_modFolder = (slash != std::string::npos) ? p.substr(0, slash) : p;

        if (FILE* f = fopen("C:\\Users\\Public\\kenshi_ai_load.txt", "w"))
        {
            fprintf(f, "kenshi_ai.dll DLL_PROCESS_ATTACH\nmodFolder=%s\n",
                    g_modFolder.c_str());
            fclose(f);
        }
    }
    return TRUE;
}
