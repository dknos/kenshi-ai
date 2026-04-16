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

// ── Hook: Dialogue::update ────────────────────────────────────────────────────

static void hook_dialogueUpdate(Dialogue* self, float frameTime)
{
    orig_dialogueUpdate(self, frameTime);

    // Diagnostic: confirm hook fires at all (writes once, no keypress needed).
    {
        static bool g_hookFired = false;
        if (!g_hookFired) {
            g_hookFired = true;
            if (FILE* f = fopen("C:\\Users\\Public\\kenshi_ai_hookfired.txt", "w")) {
                fprintf(f, "hook_dialogueUpdate fired\nself=%p frameTime=%f\n",
                        (void*)self, frameTime);
                fclose(f);
            }
        }
    }

    // Insert: open player-input dialog when any conversation is active.
    // g_f9WasDown is always updated so the edge-detect works across all
    // Dialogue::update calls (multiple NPCs are updated each frame).
    {
        bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        // Use g_recentNPCs as fallback if hook_startPlayerConversation missed 1.0.68
        if (!g_activeDlg && !g_recentNPCs.empty()) {
            g_activeDlg = g_recentNPCs.back().dlg;
            g_activeNpc = g_recentNPCs.back().chr;
        }
        if (insertDown && !g_f9WasDown)
        {
            // Diagnostic: write once so we can confirm GetAsyncKeyState + hook are working
            static bool g_diagWritten = false;
            if (!g_diagWritten) {
                g_diagWritten = true;
                if (FILE* f = fopen("C:\\Users\\Public\\kenshi_ai_diag.txt", "w")) {
                    fprintf(f, "hook_dialogueUpdate reached Insert\n"
                               "g_activeDlg=%p\ng_activeNpc=%p\n"
                               "g_recentNPCs.size=%zu\n",
                               (void*)g_activeDlg, (void*)g_activeNpc,
                               g_recentNPCs.size());
                    fclose(f);
                }
            }
        }
        if (insertDown && !g_f9WasDown && g_activeNpc)
        {
            Dialogue*  dlg    = g_activeDlg;
            Character* npc    = g_activeNpc;
            Character* player = (ou && ou->player) ? ou->player->getAnyPlayerCharacter() : nullptr;

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
        g_f9WasDown = insertDown;
    }

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

    // Radiant D2D: fire ambient NPC exchange when the timer fires.
    g_radiantAccumS += frameTime;
    if (g_radiantAccumS >= g_radiantIntervalS && g_recentNPCs.size() >= 2)
    {
        g_radiantAccumS = 0.f;

        // Pick two distinct cached NPCs.
        const CachedNPC& a = g_recentNPCs[g_recentNPCs.size() - 1];
        const CachedNPC& b = g_recentNPCs[g_recentNPCs.size() - 2];

        if (a.chr && b.chr)
        {
            // Build D2D JSON manually (mirrors D2DRequest schema).
            auto esc = [](const std::string& s) -> std::string {
                std::string o; o.reserve(s.size());
                for (char c : s) {
                    if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else o += c;
                } return o;
            };
            std::string npcAId   = std::to_string(reinterpret_cast<uintptr_t>(a.chr));
            std::string npcBId   = std::to_string(reinterpret_cast<uintptr_t>(b.chr));
            std::string npcAName = a.chr->getName();
            std::string npcBName = b.chr->getName();
            // RaceData has no name field; getFaction via ActivePlatoon::me (RootObjectBase)
            std::string npcARace = "Unknown";
            std::string npcBRace = "Unknown";
            auto getFac = [](Character* c) -> std::string {
                if (!c || !c->getPlatoon() || !c->getPlatoon()->me) return "";
                Faction* f = c->getPlatoon()->me->getFaction();
                return f ? f->getName() : "";
            };
            std::string npcAFac  = getFac(a.chr);
            std::string npcBFac  = getFac(b.chr);

            std::ostringstream j;
            j << "{"
              << "\"npc_a_id\":\""      << esc(npcAId)   << "\","
              << "\"npc_a_name\":\""    << esc(npcAName) << "\","
              << "\"npc_a_race\":\""    << esc(npcARace) << "\","
              << "\"npc_a_faction\":\"" << esc(npcAFac)  << "\","
              << "\"npc_b_id\":\""      << esc(npcBId)   << "\","
              << "\"npc_b_name\":\""    << esc(npcBName) << "\","
              << "\"npc_b_race\":\""    << esc(npcBRace) << "\","
              << "\"npc_b_faction\":\"" << esc(npcBFac)  << "\","
              << "\"campaign_id\":\""   << esc(KenshiAI::g_config.campaignId) << "\""
              << "}";

            KenshiAI::PostD2D(j.str(), [](const std::string& json)
            {
                std::lock_guard<std::mutex> lk(g_d2dMutex);
                g_d2dQueue.push({json});
            });
        }
    }

    // Drain D2D responses: parse each line and say() it via the right Dialogue.
    {
        std::lock_guard<std::mutex> lk(g_d2dMutex);
        while (!g_d2dQueue.empty())
        {
            std::string json = std::move(g_d2dQueue.front().json);
            g_d2dQueue.pop();

            // Minimal parse of {"lines":[{"speaker_id":"...","text":"..."},...]}
            size_t pos = 0;
            while ((pos = json.find("\"speaker_id\"", pos)) != std::string::npos)
            {
                auto idStart = json.find('"', pos + 13);
                auto idEnd   = json.find('"', idStart + 1);
                auto txtStart = json.find("\"text\"", idEnd);
                if (txtStart == std::string::npos) break;
                auto qs = json.find('"', txtStart + 7);
                auto qe = json.find('"', qs + 1);
                if (idStart == std::string::npos || idEnd == std::string::npos ||
                    qs == std::string::npos || qe == std::string::npos) break;

                std::string speakerId = json.substr(idStart + 1, idEnd - idStart - 1);
                std::string text      = json.substr(qs + 1, qe - qs - 1);

                // Find the cached Dialogue for this speaker.
                for (auto& e : g_recentNPCs)
                {
                    if (e.dlg && e.chr &&
                        std::to_string(reinterpret_cast<uintptr_t>(e.chr)) == speakerId)
                    {
                        e.dlg->say(text, nullptr);
                        break;
                    }
                }
                pos = qe + 1;
            }
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
            reinterpret_cast<uintptr_t>(GetModuleHandleA("kenshi_x64.exe")) + 0x683BA0);

        KenshiLib::AddHook(
            spcAddr,
            (void*)hook_startPlayerConversation,
            (void**)&orig_startPlayerConversation
        );

        // RVA 0x684910 from KenshiLib/Include/kenshi/Dialogue.h (1.0.65).
        // GetRealAddress(&Dialogue::update) triggers a FUNC_BEGIN/FUNC_END
        // assertion failure during preload — use the raw RVA instead.
        void* updateAddr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(GetModuleHandleA("kenshi_x64.exe")) + 0x684910);
        KenshiLib::AddHook(
            updateAddr,
            (void*)hook_dialogueUpdate,
            (void**)&orig_dialogueUpdate
        );

        g_radiantIntervalS = KenshiAI::g_config.radiantDelayS;

        InputDialog::Init();
    }
}
