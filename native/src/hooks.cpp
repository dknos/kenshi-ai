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

// ── Response queue ────────────────────────────────────────────────────────────

struct QueuedResponse
{
    Dialogue*   dialogue;   // which NPC's Dialogue object to reply into
    Character*  character;  // the NPC character
    KenshiAI::ParsedResponse parsed;
};

static std::mutex              g_queueMutex;
static std::queue<QueuedResponse> g_responseQueue;

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

    // Grab the player character from the game's player interface.
    Character* player = nullptr;
    if (ou && ou->playerFaction && !ou->playerFaction->characters.empty())
        player = ou->playerFaction->characters[0]; // squad slot 0 -> active speaker

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
    std::lock_guard<std::mutex> lk(g_queueMutex);
    while (!g_responseQueue.empty())
    {
        QueuedResponse& qr = g_responseQueue.front();
        if (qr.dialogue == self)
        {
            Actions::DispatchResponse(qr.dialogue, qr.character, qr.parsed);
            g_responseQueue.pop();
        }
        else
        {
            break; // Not ours — leave for the right Dialogue's update tick.
        }
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

namespace Hooks
{
    void Init()
    {
        KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Dialogue::startPlayerConversation),
            (void*)hook_startPlayerConversation,
            (void**)&orig_startPlayerConversation
        );

        KenshiLib::AddHook(
            KenshiLib::GetRealAddress(&Dialogue::update),
            (void*)hook_dialogueUpdate,
            (void**)&orig_dialogueUpdate
        );
    }
}
