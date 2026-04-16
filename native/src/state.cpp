/**
 * state.cpp — Extract Character state into a JSON ChatRequest for the sidecar.
 *
 * Only reads KenshiLib-exposed fields and getter methods.  Does NOT read save
 * files (our design pushes state through the hook, not save_reader.py).
 */

#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Faction.h>
#include <kenshi/FactionRelations.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Platoon.h>

#include "kenshi_ai.h"

#include <sstream>
#include <string>
#include <algorithm>

// Minimal JSON escaping for the values we put in the request.
static std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

namespace State
{
    // getFaction via ActivePlatoon->me (Platoon inherits RootObjectBase::getFaction)
    static Faction* getFactionOf(Character* c)
    {
        if (!c) return nullptr;
        ActivePlatoon* ap = c->getPlatoon();
        if (!ap || !ap->me) return nullptr;
        return ap->me->getFaction();
    }

    std::string BuildChatRequest(Character* npc, Character* player,
                                 const std::string& playerMessage)
    {
        if (!npc) return "{}";

        // ── NPC fields ─────────────────────────────────────────────────────
        std::string npcId   = std::to_string(reinterpret_cast<uintptr_t>(npc));
        std::string npcName = npc->getName();

        // Race: KenshiLib RaceData has no direct name field; skip for now
        std::string race = "Unknown";

        std::string faction;
        if (Faction* f = getFactionOf(npc))
            faction = f->getName();

        // Health: CharStats doesn't expose raw HP (it's per-limb in MedicalSystem).
        // Use wantsToEatNow() as a proxy for hunger.
        float healthFrac = 1.0f;
        float hungerFrac = npc->wantsToEatNow() ? 1.0f : 0.0f;

        // ── Player fields ──────────────────────────────────────────────────
        std::string playerFaction;
        int playerFactionRel = 0;

        if (player)
        {
            if (Faction* pf = getFactionOf(player))
                playerFaction = pf->getName();

            // FactionRelations::getRelationData(Faction*) returns RelationData* —
            // skip relation value for now; sidecar tracks it in memory instead.
        }

        int opinion = KenshiAI::GetOpinion(reinterpret_cast<uintptr_t>(npc));

        // ── Build JSON manually (no external json lib dependency) ──────────
        std::ostringstream j;
        j << "{"
          << "\"npc_id\":\""           << jsonEscape(npcId)          << "\","
          << "\"npc_name\":\""         << jsonEscape(npcName)        << "\","
          << "\"race\":\""             << jsonEscape(race)           << "\","
          << "\"faction\":\""          << jsonEscape(faction)        << "\","
          << "\"health_frac\":"        << healthFrac                 << ","
          << "\"hunger_frac\":"        << hungerFrac                 << ","
          << "\"player_faction\":\""   << jsonEscape(playerFaction)  << "\","
          << "\"player_faction_rel\":" << playerFactionRel           << ","
          << "\"opinion\":"            << opinion                    << ","
          << "\"campaign_id\":\""      << jsonEscape(KenshiAI::g_config.campaignId) << "\","
          << "\"message\":\""          << jsonEscape(playerMessage)  << "\""
          << "}";

        return j.str();
    }
}
