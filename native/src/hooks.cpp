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
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Platoon.h>
#include <kenshi/RaceData.h>
#include <kenshi/Faction.h>
#include <core/Functions.h>

#include "kenshi_ai.h"
#include "input_dialog.h"

#include <mutex>
#include <queue>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

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

// Recent-NPC cache — populated by hook_startPlayerConversation.
// Radiant scan picks two entries for ambient D2D without needing
// full GameWorld character enumeration.
struct CachedNPC { Dialogue* dlg; Character* chr; };
static std::vector<CachedNPC> g_recentNPCs;
static constexpr size_t RECENT_NPC_CACHE = 8;

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

// ── Active conversation tracking (set in startPlayerConversation) ─────────────

static Dialogue*  g_activeDlg  = nullptr;
static Character* g_activeNpc  = nullptr;
static bool       g_f9WasDown  = false;

// ── Hook originals ────────────────────────────────────────────────────────────

static bool (*orig_startPlayerConversation)(Dialogue*, Character*, DialogLineData*) = nullptr;
static void (*orig_dialogueUpdate)(Dialogue*, float)                               = nullptr;
static void (*orig_mainLoop)(GameWorld*, float)                                    = nullptr;

// ── Hook: startPlayerConversation ─────────────────────────────────────────────

static bool hook_startPlayerConversation(Dialogue* self,
                                          Character* target,
                                          DialogLineData* talk)
{
    // Let the game set up its normal conversation structures.
    bool result = orig_startPlayerConversation(self, target, talk);

    if (!result || !target)
        return result;

    // Track active conversation for F9 input.
    g_activeDlg = self;
    g_activeNpc = target;

    // Grab the player character from PlayerInterface.
    Character* player = (ou && ou->player) ? ou->player->getAnyPlayerCharacter() : nullptr;

    // Update recent-NPC cache for the radiant D2D scan.
    bool found = false;
    for (auto& e : g_recentNPCs) { if (e.chr == target) { found = true; break; } }
    if (!found) {
        if (g_recentNPCs.size() >= RECENT_NPC_CACHE) g_recentNPCs.erase(g_recentNPCs.begin());
        g_recentNPCs.push_back({self, target});
    }

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

// ── Shared drain logic (called from main-loop hook) ───────────────────────────

static void DrainQueues()
{
    // Drain AI chat responses — dispatch each to the right NPC's Dialogue.
    {
        std::lock_guard<std::mutex> lk(g_queueMutex);
        while (!g_responseQueue.empty())
        {
            QueuedResponse qr = std::move(g_responseQueue.front());
            g_responseQueue.pop();
            if (qr.dialogue && qr.character)
                Actions::DispatchResponse(qr.dialogue, qr.character, qr.parsed);
        }
    }

    // Drain D2D responses.
    {
        std::lock_guard<std::mutex> lk(g_d2dMutex);
        while (!g_d2dQueue.empty())
        {
            std::string json = std::move(g_d2dQueue.front().json);
            g_d2dQueue.pop();

            size_t pos = 0;
            while ((pos = json.find("\"speaker_id\"", pos)) != std::string::npos)
            {
                auto idStart  = json.find('"', pos + 13);
                auto idEnd    = json.find('"', idStart + 1);
                auto txtStart = json.find("\"text\"", idEnd);
                if (txtStart == std::string::npos) break;
                auto qs = json.find('"', txtStart + 7);
                auto qe = json.find('"', qs + 1);
                if (idStart == std::string::npos || idEnd == std::string::npos ||
                    qs     == std::string::npos || qe    == std::string::npos) break;

                std::string speakerId = json.substr(idStart + 1, idEnd - idStart - 1);
                std::string text      = json.substr(qs + 1, qe - qs - 1);
                for (auto& e : g_recentNPCs)
                {
                    if (e.dlg && e.chr &&
                        std::to_string(reinterpret_cast<uintptr_t>(e.chr)) == speakerId)
                    { e.dlg->say(text, nullptr); break; }
                }
                pos = qe + 1;
            }
        }
    }
}

// ── Hook: GameWorld::mainLoop_GPUSensitiveStuff — fires every render frame ────

static void hook_mainLoop(GameWorld* self, float time)
{
    orig_mainLoop(self, time);
    DrainQueues();
}

// ── Hook: Dialogue::update — kept for radiant D2D timer ──────────────────────

static void hook_dialogueUpdate(Dialogue* self, float frameTime)
{
    orig_dialogueUpdate(self, frameTime);

    // Radiant D2D: fire ambient NPC exchange when timer fires.
    g_radiantAccumS += frameTime;
    if (g_radiantAccumS >= g_radiantIntervalS && g_recentNPCs.size() >= 2)
    {
        g_radiantAccumS = 0.f;
        const CachedNPC& a = g_recentNPCs[g_recentNPCs.size() - 1];
        const CachedNPC& b = g_recentNPCs[g_recentNPCs.size() - 2];
        if (a.chr && b.chr)
        {
            auto esc = [](const std::string& s) -> std::string {
                std::string o; o.reserve(s.size());
                for (char c : s) {
                    if (c=='"') o+="\\\""; else if (c=='\\') o+="\\\\"; else o+=c;
                } return o;
            };
            auto getFac = [](Character* c) -> std::string {
                if (!c || !c->getPlatoon() || !c->getPlatoon()->me) return "";
                Faction* f = c->getPlatoon()->me->getFaction();
                return f ? f->getName() : "";
            };
            std::ostringstream j;
            j << "{\"npc_a_id\":\""      << esc(std::to_string(reinterpret_cast<uintptr_t>(a.chr))) << "\""
              << ",\"npc_a_name\":\""    << esc(a.chr->getName()) << "\""
              << ",\"npc_a_race\":\"Unknown\""
              << ",\"npc_a_faction\":\"" << esc(getFac(a.chr)) << "\""
              << ",\"npc_b_id\":\""      << esc(std::to_string(reinterpret_cast<uintptr_t>(b.chr))) << "\""
              << ",\"npc_b_name\":\""    << esc(b.chr->getName()) << "\""
              << ",\"npc_b_race\":\"Unknown\""
              << ",\"npc_b_faction\":\"" << esc(getFac(b.chr)) << "\""
              << ",\"campaign_id\":\""   << esc(KenshiAI::g_config.campaignId) << "\""
              << "}";
            KenshiAI::PostD2D(j.str(), [](const std::string& json) {
                std::lock_guard<std::mutex> lk(g_d2dMutex);
                g_d2dQueue.push({json});
            });
        }
    }
}

// ── Background Insert-key poller ──────────────────────────────────────────────

static void InsertKeyThread()
{
    bool wasDown = false;
    while (true)
    {
        Sleep(50);
        bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (down && !wasDown)
        {
            // Snapshot NPC context under lock-free read (pointers are set atomically
            // enough for a polling thread — worst case we miss one frame).
            Dialogue*  dlg    = g_activeDlg;
            Character* npc    = g_activeNpc;
            if (!npc && !g_recentNPCs.empty()) {
                dlg = g_recentNPCs.back().dlg;
                npc = g_recentNPCs.back().chr;
            }
            Character* player = (ou && ou->player)
                                ? ou->player->getAnyPlayerCharacter() : nullptr;

            if (npc)
            {
                InputDialog::Show([dlg, npc, player](const std::string& text)
                {
                    std::string req = State::BuildChatRequest(npc, player, text);
                    KenshiAI::PostChat(req, [dlg, npc](const std::string& json)
                    {
                        QueuedResponse qr;
                        qr.dialogue  = dlg;
                        qr.character = npc;
                        qr.parsed    = KenshiAI::ParseResponse(json);
                        std::lock_guard<std::mutex> lk(g_queueMutex);
                        g_responseQueue.push(std::move(qr));
                    });
                });
            }
        }
        wasDown = down;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

namespace Hooks
{
    void Init()
    {
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA("kenshi_x64.exe"));

        // startPlayerConversation — RVA 0x683BA0 from Dialogue.h
        KenshiLib::AddHook(
            reinterpret_cast<void*>(base + 0x683BA0),
            (void*)hook_startPlayerConversation,
            (void**)&orig_startPlayerConversation
        );

        // GameWorld::mainLoop_GPUSensitiveStuff — RVA 0x7877A0, called every frame.
        // Used to drain g_responseQueue on the main thread.
        KenshiLib::AddHook(
            reinterpret_cast<void*>(base + 0x7877A0),
            (void*)hook_mainLoop,
            (void**)&orig_mainLoop
        );

        // Dialogue::update — RVA 0x684910, only called during active conversations.
        // Kept for radiant D2D timer; not needed for Insert key or queue drain.
        KenshiLib::AddHook(
            reinterpret_cast<void*>(base + 0x684910),
            (void*)hook_dialogueUpdate,
            (void**)&orig_dialogueUpdate
        );

        g_radiantIntervalS = KenshiAI::g_config.radiantDelayS;

        InputDialog::Init();

        // Background thread polls Insert key at 50ms intervals — independent of
        // Dialogue::update which only fires during active conversations.
        std::thread(InsertKeyThread).detach();
    }
}
