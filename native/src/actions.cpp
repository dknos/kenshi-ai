/**
 * actions.cpp — Dispatch parsed LLM response to in-engine Kenshi actions.
 *
 * All functions called from hooks.cpp::hook_dialogueUpdate() — game main thread.
 *
 * Each Pydantic action kind maps to a Kenshi DialogActionEnum or a direct
 * API call. The mapping is documented inline.
 */

#include <kenshi/Dialogue.h>
#include <kenshi/Character.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>

#include "kenshi_ai.h"

namespace Actions
{
    void DispatchResponse(Dialogue* dlg, Character* npc,
                          const KenshiAI::ParsedResponse& resp)
    {
        if (!dlg || !npc) return;

        // ── speak ───────────────────────────────────────────────────────────
        // Dialogue::say(text, line) renders the speech bubble and broadcasts
        // to nearby characters.  Passing nullptr for line is fine for a
        // free-form injected line.
        if (!resp.speakText.empty())
            dlg->say(resp.speakText, nullptr);

        // ── recruit_accept → DA_JOIN_SQUAD_FAST ────────────────────────────
        // Dialogue::_doActions() is private; dispatching a DialogActionEnum
        // requires constructing a DialogLineData with a populated lektor<Action*>
        // and calling the public say(DialogLineData*) overload.  The lektor API
        // is not yet reverse-engineered.  Phase 5 will wire this properly.
        // For now the LLM's spoken acceptance line is the only observable effect.

        // ── recruit_decline — speech line is sufficient ────────────────────

        // ── follow → DA_FOLLOW_WHILE_TALKING ──────────────────────────────
        // Same DialogLineData constraint as recruit.  Phase 5 TODO.

        // ── idle → DA_CLEAR_AI ─────────────────────────────────────────────
        // Same DialogLineData constraint.  Phase 5 TODO.

        // ── flee → DA_RUN_AWAY ─────────────────────────────────────────────
        // EV_SQUAD_BROKEN fires the NPC's "squad routed" response which
        // triggers flee AI.  Nearest available event without DialogLineData.
        if (resp.flee)
            dlg->sendEventOverride(npc, EV_SQUAD_BROKEN, true);

        // ── call_guards → DA_CRIME_ALARM ──────────────────────────────────
        if (resp.callGuards)
            dlg->sendEventOverride(npc, EV_SOUND_THE_ALARM, true);

        // ── attack_target → DA_ATTACK_CHASE_FOREVER ───────────────────────
        if (resp.attackTarget)
            dlg->sendEventOverride(npc, EV_LAUNCH_ATTACK, true);

        // ── give_item / take_item / transfer_cats ─────────────────────────
        // These require Inventory access; stubbed for post-MVP.
        // Inventory::giveItem(Character* recipient, GameData* item, int qty)
        // exists in KenshiLib.  Will wire in Phase 5.

        // ── opinion_delta ─────────────────────────────────────────────────
        if (resp.opinionDelta != 0)
        {
            // CharacterMemory stores per-character opinion.
            // npc->memory->changeOpinionOfCharacter(player, resp.opinionDelta)
            // TODO: resolve player character pointer from ou->playerFaction.
        }

        // ── faction_relation_delta ────────────────────────────────────────
        if (resp.factionRelDelta != 0 && !resp.factionRelTarget.empty())
        {
            // FactionRelations::changeRelationsBy(Faction*, float delta)
            // TODO: resolve faction by name from ou->factionList.
        }
    }
}
