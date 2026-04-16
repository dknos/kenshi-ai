/**
 * hooks.cpp — Dialogue hooks + main-thread response queue.
 *
 * Strategy:
 *   1. Hook Dialogue::startPlayerConversation — intercept player-initiated chat.
 *      Build character-state JSON, fire non-blocking HTTP POST to sidecar.
 *   2. Hook Dialogue::update — runs every frame on main thread; drain the
 *      response queue here and call Dialogue::say() + dispatch actions.
 *   3. Hook Dialogue::say (the string overload) — capture ambient/NPC speech
 *      for world-synthesis event logging (optional, post-MVP).
 *
 * Thread safety: g_responseQueue is guarded by g_queueMutex.
 * All game object access happens exclusively inside the update hook (main thread).
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <kenshi/Dialogue.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <core/Functions.h>

#include "kenshi_ai.h"

#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <chrono>

// ── Response queue ────────────────────────────────────────────────────────────

struct QueuedResponse
{
    Dialogue*   dialogue;   // which NPC's Dialogue object to reply into
    Character*  character;  // the NPC character
    KenshiAI::ParsedResponse parsed;
};

static std::mutex              g_queueMutex;
static std::queue<QueuedResponse> g_responseQueue;

// ── D2D response queue ────────────────────────────────────────────────────────

struct QueuedD2DResponse
{
    std::string json;  // raw JSON — dispatched to nearby NPC say() calls
};

static std::mutex                    g_d2dMutex;
static std::queue<QueuedD2DResponse> g_d2dQueue;

// Radiant timer — fire D2D once per g_radiantIntervalS game-seconds elapsed.
static float g_radiantAccumS     = 0.f;
static float g_radiantIntervalS  = 240.f;  // overridden from config in Init()

// ── Actions (defined in actions.cpp) ─────────────────────────────────────────
namespace Actions
{
    void DispatchResponse(Dialogue* dlg, Character* npc,
                         const KenshiAI::ParsedResponse& resp);
}

// ── State extraction (defined in state.cpp) ───────────────────────────────────
namespace State
{
    std::string BuildChatRequest(Character* npc, Character* player,
                                 const std::string& playerMessage);
}

// ── Hook originals ────────────────────────────────────────────────────────────

static bool (*orig_startPlayerConversation)(Dialogue*, Character*, DialogLineData*) = nullptr;
static void (*orig_dialogueUpdate)(Dialogue*, float)                               = nullptr;
// optional ambient hook:
static void (*orig_dialogueSay)(Dialogue*, const std::string&, DialogLineData*)    = nullptr;

// ── Hook: startPlayerConversation ─────────────────────────────────────────────

static bool hook_startPlayerConversation(Dialogue* self,
                                          Character* target,
                                          DialogLineData* talk)
{
    // Let the game set up its normal conversation structures.
    bool result = orig_startPlayerConversation(self, target, talk);

    if (!result || !target)
        return result;

    // Grab the player character from PlayerInterface.
    Character* player = (ou && ou->player) ? ou->player->getAnyPlayerCharacter() : nullptr;

    // Build JSON request and fire async HTTP.
    // The player's actual typed message isn't intercepted here — we trigger
    // on conversation open with a greeting context.  The text-input hook (UI
    // layer, TODO) will send the real typed message.
    std::string requestJson = State::BuildChatRequest(target, player, "__greet__");

    Dialogue* dlgCapture  = self;
    Character* npcCapture = target;

    KenshiAI::PostChat(requestJson, [dlgCapture, npcCapture](const std::string& json)
    {
        QueuedResponse qr;
        qr.dialogue  = dlgCapture;
        qr.character = npcCapture;
        qr.parsed    = KenshiAI::ParseResponse(json);

        std::lock_guard<std::mutex> lk(g_queueMutex);
        g_responseQueue.push(std::move(qr));
    });

    return result;
}

// ── Hook: Dialogue::update ────────────────────────────────────────────────────

static void hook_dialogueUpdate(Dialogue* self, float frameTime)
{
    orig_dialogueUpdate(self, frameTime);

    // Drain responses destined for this Dialogue instance.
    // We can't break on mismatches because std::queue is FIFO — a response for
    // a different Dialogue* would permanently block everything behind it.
    // Move unmatched items to a temp queue and put them back after.
    std::queue<QueuedResponse> unmatched;
    {
        std::lock_guard<std::mutex> lk(g_queueMutex);
        while (!g_responseQueue.empty())
        {
            QueuedResponse qr = std::move(g_responseQueue.front());
            g_responseQueue.pop();
            if (qr.dialogue == self)
                Actions::DispatchResponse(qr.dialogue, qr.character, qr.parsed);
            else
                unmatched.push(std::move(qr));
        }
        while (!unmatched.empty())
        {
            g_responseQueue.push(std::move(unmatched.front()));
            unmatched.pop();
        }
    }

    // Radiant D2D: accumulate time and fire ambient NPC exchanges.
    g_radiantAccumS += frameTime;
    if (g_radiantAccumS >= g_radiantIntervalS)
    {
        g_radiantAccumS = 0.f;
        // TODO Phase 6: scan nearby NPCs, build D2D request, PostD2D(json, cb).
        // Stub present — wires the timer infrastructure.
    }

    // Drain pending D2D responses.
    {
        std::lock_guard<std::mutex> lk(g_d2dMutex);
        while (!g_d2dQueue.empty())
        {
            // TODO Phase 6: parse D2DResponse JSON and dispatch say() calls.
            g_d2dQueue.pop();
        }
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

namespace Hooks
{
    void Init()
    {
        // startPlayerConversation is private in the game class — member pointer
        // syntax won't compile outside the class.  Use the known RVA instead.
        // RVA 0x683BA0 from KenshiLib/Include/kenshi/Dialogue.h.
        void* spcAddr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(GetModuleHandle("kenshi_x64.exe")) + 0x683BA0);

        KenshiLib::AddHook(
            spcAddr,
            (void*)hook_startPlayerConversation,
            (void**)&orig_startPlayerConversation
        );

        KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Dialogue::update),
            (void*)hook_dialogueUpdate,
            (void**)&orig_dialogueUpdate
        );

        g_radiantIntervalS = KenshiAI::g_config.radiantDelayS;
    }
}
