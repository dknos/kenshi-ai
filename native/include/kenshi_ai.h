#pragma once

// Shared declarations for the kenshi_ai native plugin.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <functional>

namespace KenshiAI
{
    // Called once by ipc.cpp when an async chat response arrives.
    // Runs on background thread — callback is pushed to main-thread queue.
    using ResponseCallback = std::function<void(const std::string& json)>;

    // ---------- IPC ----------------------------------------------------------
    // Non-blocking: spawns a thread, fires callback on completion.
    void PostChat(const std::string& requestJson, ResponseCallback cb);
    void PostD2D(const std::string& requestJson, ResponseCallback cb);

    // ---------- Actions ------------------------------------------------------
    // Called from the main-thread queue drain in hooks.cpp.
    // All functions must be called from the game main thread.

    struct ParsedResponse
    {
        std::string npcId;
        std::string speakText;
        bool recruitAccept   = false;
        bool recruitDecline  = false;
        bool follow          = false;
        bool idle            = false;
        bool flee            = false;
        bool callGuards      = false;
        bool attackTarget    = false;
        std::string attackTargetId;
        std::string giveItemName;
        int  giveItemQty     = 0;
        std::string takeItemName;
        int  takeItemQty     = 0;
        int  transferCats    = 0;   // +NPC pays player, -player pays NPC
        int  opinionDelta    = 0;
        int  opinionAfter    = 0;   // absolute opinion returned by sidecar
        int  factionRelDelta = 0;
        std::string factionRelTarget;
    };

    ParsedResponse ParseResponse(const std::string& json);

    // ---------- Config -------------------------------------------------------
    struct Config
    {
        std::string serverUrl   = "http://127.0.0.1:9392";
        int         timeoutMs   = 30000;
        std::string defaultModel;
        std::string campaignId  = "Default";
        int         talkRadius  = 100;
        int         proximityRadius = 200;
        float       radiantDelayS   = 240.0f;
        float       synthesisIntervalMin = 5.0f;
    };

    extern Config g_config;
    void LoadConfig(const std::string& modFolder);

    // ---------- Opinion cache ------------------------------------------------
    // Tracks the last-known opinion per NPC (keyed by Character pointer address).
    // Persists within a game session; survives multiple conversations with the
    // same NPC.  Reset when the campaign_id changes.
    int  GetOpinion(uintptr_t npcAddr);
    void SetOpinion(uintptr_t npcAddr, int value);
}
