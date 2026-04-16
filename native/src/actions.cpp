/**
 * actions.cpp — Dispatch parsed LLM response to in-engine Kenshi actions.
 *
 * All functions called from hooks.cpp::hook_dialogueUpdate() — game main thread.
 */

#include <kenshi/Dialogue.h>
#include <kenshi/Character.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/Platoon.h>

#include "kenshi_ai.h"

#include <algorithm>

// ── _doActions helper ─────────────────────────────────────────────────────────
//
// Dialogue::_doActions(DialogLineData*) is private — call it via its known RVA.
// RVA 0x67FDE0 from KenshiLib/Include/kenshi/Dialogue.h.
//
// We pass a zero-initialised 0x280-byte surrogate for DialogLineData.
// _doActions only reads the actions lektor at offset 0x208:
//   uint32_t count   @ 0x208
//   uint32_t maxSize @ 0x20C
//   T**      stuff   @ 0x210   (T = DialogLineData::DialogAction* = {int key, int value})
//
// The lektor stores pointers-to-actions, so stuff[0] is a pointer to the single
// RawAction struct on the stack.  All other DialogLineData fields stay zero;
// _doActions doesn't touch them for the action enums we use here.

namespace
{
    using DoActionsFunc = void(*)(Dialogue*, void*);

    DoActionsFunc GetDoActions()
    {
        static DoActionsFunc fn = reinterpret_cast<DoActionsFunc>(
            reinterpret_cast<uintptr_t>(GetModuleHandleA("kenshi_x64.exe")) + 0x67FDE0);
        return fn;
    }

    struct RawAction { int key; int value; };

    void InvokeAction(Dialogue* dlg, int actionEnum, int value = 0)
    {
        if (!dlg) return;
        RawAction entry{ actionEnum, value };
        RawAction* pEntry = &entry;

        alignas(16) char buf[0x280] = {};
        *reinterpret_cast<uint32_t*>(buf + 0x208) = 1;
        *reinterpret_cast<uint32_t*>(buf + 0x20C) = 1;
        *reinterpret_cast<RawAction***>(buf + 0x210) = &pEntry;

        GetDoActions()(dlg, buf);
    }
}

namespace Actions
{
    void DispatchResponse(Dialogue* dlg, Character* npc,
                          const KenshiAI::ParsedResponse& resp)
    {
        if (!dlg || !npc) return;

        Character* player = (ou && ou->player) ? ou->player->getAnyPlayerCharacter() : nullptr;

        // ── speak ────────────────────────────────────────────────────────────
        if (!resp.speakText.empty())
            dlg->say(resp.speakText, nullptr);

        // ── recruit_accept → PlayerInterface::recruit() ──────────────────────
        if (resp.recruitAccept && ou && ou->player)
            ou->player->recruit(npc, false);

        // ── follow → DA_FOLLOW_WHILE_TALKING ─────────────────────────────────
        if (resp.follow)
            InvokeAction(dlg, static_cast<int>(DA_FOLLOW_WHILE_TALKING));

        // ── idle → DA_CLEAR_AI ────────────────────────────────────────────────
        if (resp.idle)
            InvokeAction(dlg, static_cast<int>(DA_CLEAR_AI));

        // ── flee → EV_SQUAD_BROKEN ────────────────────────────────────────────
        if (resp.flee)
            dlg->sendEventOverride(npc, EV_SQUAD_BROKEN, true);

        // ── call_guards → EV_SOUND_THE_ALARM ─────────────────────────────────
        if (resp.callGuards)
            dlg->sendEventOverride(npc, EV_SOUND_THE_ALARM, true);

        // ── attack_target → EV_LAUNCH_ATTACK ─────────────────────────────────
        if (resp.attackTarget)
            dlg->sendEventOverride(npc, EV_LAUNCH_ATTACK, true);

        // ── transfer_cats ─────────────────────────────────────────────────────
        // DA_GIVE_MONEY (enum 9) / DA_TAKE_MONEY (enum 8) with value = amount.
        // Positive = NPC pays player; negative = player pays NPC.
        if (resp.transferCats != 0)
        {
            if (resp.transferCats > 0)
                InvokeAction(dlg, static_cast<int>(DA_GIVE_MONEY), resp.transferCats);
            else
                InvokeAction(dlg, static_cast<int>(DA_TAKE_MONEY), -resp.transferCats);
        }

        // ── give_item — NPC gives item from own inventory to player ───────────
        if (!resp.giveItemName.empty() && resp.giveItemQty > 0 && player)
        {
            Inventory* npcInv    = npc->inventory;
            Inventory* playerInv = player->inventory;
            if (npcInv && playerInv)
            {
                const auto& all = npcInv->getAllItems();
                for (uint32_t i = 0; i < all.size(); ++i)
                {
                    Item* item = all[i];
                    if (!item) continue;
                    if (item->getName() == resp.giveItemName)
                    {
                        Item* removed = npcInv->removeItemDontDestroy_returnsItem(
                            item, resp.giveItemQty, true);
                        if (removed)
                            playerInv->addItem(removed, resp.giveItemQty, true, false);
                        break;
                    }
                }
            }
        }

        // ── take_item — NPC takes item from player inventory ──────────────────
        if (!resp.takeItemName.empty() && resp.takeItemQty > 0 && player)
        {
            Inventory* npcInv    = npc->inventory;
            Inventory* playerInv = player->inventory;
            if (npcInv && playerInv)
            {
                const auto& all = playerInv->getAllItems();
                for (uint32_t i = 0; i < all.size(); ++i)
                {
                    Item* item = all[i];
                    if (!item) continue;
                    if (item->getName() == resp.takeItemName)
                    {
                        Item* removed = playerInv->removeItemDontDestroy_returnsItem(
                            item, resp.takeItemQty, true);
                        if (removed)
                            npcInv->addItem(removed, resp.takeItemQty, true, false);
                        break;
                    }
                }
            }
        }

        // ── opinion_delta — update session-local opinion cache ────────────────
        if (resp.opinionDelta != 0)
        {
            uintptr_t key = reinterpret_cast<uintptr_t>(npc);
            int cur = KenshiAI::GetOpinion(key);
            KenshiAI::SetOpinion(key, cur + resp.opinionDelta);
        }

        // ── opinion_after — sidecar returns absolute post-turn value ──────────
        // Always update so the next turn's request carries the correct opinion.
        {
            uintptr_t key = reinterpret_cast<uintptr_t>(npc);
            KenshiAI::SetOpinion(key, resp.opinionAfter);
        }

        // ── faction_relation_delta ────────────────────────────────────────────
        if (resp.factionRelDelta != 0)
        {
            // getPlatoon() returns ActivePlatoon*; getFaction() is on Platoon (RootObjectBase)
            Faction* npcFaction    = (npc->getPlatoon() && npc->getPlatoon()->me)
                                     ? npc->getPlatoon()->me->getFaction() : nullptr;
            Faction* playerFaction = (ou && ou->player) ? ou->player->getFaction() : nullptr;
            // Faction::relations is the FactionRelations* at offset 0x78 (Faction.h)
            if (npcFaction && playerFaction && npcFaction->relations)
            {
                npcFaction->relations->affectRelations(
                    playerFaction,
                    static_cast<float>(resp.factionRelDelta),
                    1.0f
                );
            }
        }
    }
}
