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
        if (resp.recruitAccept)
        {
            DialogLineData::DialogAction action;
            action.key   = DA_JOIN_SQUAD_FAST;
            action.value = 0;
            // _doActions is private; workaround: inject a synthetic line via
            // sendEvent with a pre-built one-line dialogue package (post-MVP).
            // For now: trigger the built-in "JOIN" event which fires the same code.
            dlg->sendEventOverride(npc, EV_DIALOG_JOIN, true);
        }

        // ── recruit_decline — no engine action needed, speech already sent ──

        // ── follow → DA_FOLLOW_WHILE_TALKING ──────────────────────────────
        if (resp.follow)
            dlg->sendEventOverride(npc, EV_DIALOG_FOLLOW, true);

        // ── idle → DA_CLEAR_AI ─────────────────────────────────────────────
        if (resp.idle)
            dlg->sendEventOverride(npc, EV_DIALOG_IDLE, true);

        // ── flee → DA_RUN_AWAY ─────────────────────────────────────────────
        if (resp.flee)
            dlg->sendEventOverride(npc, EV_DIALOG_RUN_AWAY, true);

        // ── call_guards → DA_CRIME_ALARM ──────────────────────────────────
        if (resp.callGuards)
            dlg->sendEventOverride(npc, EV_DIALOG_CRIME_ALARM, true);

        // ── attack_target → DA_ATTACK_CHASE_FOREVER ───────────────────────
        if (resp.attackTarget && !resp.attackTargetId.empty())
        {
            // Resolve target by npc_id pointer (stored as address string).
            // TODO: more robust lookup via GameWorld character list.
            dlg->sendEventOverride(npc, EV_DIALOG_ATTACK, true);
        }

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
