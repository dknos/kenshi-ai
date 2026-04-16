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
    std::string BuildChatRequest(Character* npc, Character* player,
                                 const std::string& playerMessage)
    {
        if (!npc) return "{}";

        // ── NPC fields ─────────────────────────────────────────────────────
        std::string npcId   = std::to_string(reinterpret_cast<uintptr_t>(npc));
        std::string npcName = npc->getName();   // GameData name lookup

        std::string race    = "Unknown";
        std::string faction = "";

        if (npc->getRace())
            race = npc->getRace()->name;

        // Faction lives on the platoon, not directly on Character.
        if (npc->getPlatoon() && npc->getPlatoon()->getFaction())
            faction = npc->getPlatoon()->getFaction()->getName();

        // Health and hunger (0.0 – 1.0)
        float healthFrac = 1.0f;
        float hungerFrac = 0.0f;
        if (npc->stats)
        {
            float maxHp = npc->stats->getMaxHP();
            if (maxHp > 0.f)
                healthFrac = std::clamp(npc->stats->getHP() / maxHp, 0.f, 1.f);
            // CharStats has no raw hunger float; Character::wantsToEatNow() is
            // the only accessible hunger indicator from KenshiLib headers.
            hungerFrac = npc->wantsToEatNow() ? 1.0f : 0.0f;
        }

        // ── Player fields ──────────────────────────────────────────────────
        std::string playerFaction;
        int playerFactionRel = 0;

        if (player && player->getPlatoon() && player->getPlatoon()->getFaction())
            playerFaction = player->getPlatoon()->getFaction()->getName();

        {
            Faction* npcFaction    = npc->getPlatoon()    ? npc->getPlatoon()->getFaction()    : nullptr;
            Faction* playerFactionPtr = (player && player->getPlatoon()) ? player->getPlatoon()->getFaction() : nullptr;
            if (npcFaction && playerFactionPtr)
            {
                playerFactionRel = static_cast<int>(
                    npcFaction->getRelationsTowards(playerFactionPtr));
                playerFactionRel = std::clamp(playerFactionRel, -100, 100);
            }
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
