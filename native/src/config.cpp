/**
 * config.cpp — Load kenshi_ai.ini from the mod folder.
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "kenshi_ai.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace KenshiAI
{
    Config g_config;

    static std::unordered_map<std::string, std::string>
    ParseIni(const std::string& path)
    {
        std::unordered_map<std::string, std::string> kv;
        std::ifstream f(path);
        if (!f.is_open()) return kv;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // trim whitespace
            auto trim = [](std::string& s) {
                while (!s.empty() && (s.front()==' '||s.front()=='\t')) s.erase(s.begin());
                while (!s.empty() && (s.back() ==' '||s.back() =='\t')) s.pop_back();
            };
            trim(key); trim(val);
            kv[key] = val;
        }
        return kv;
    }

    void LoadConfig(const std::string& modFolder)
    {
        auto kv = ParseIni(modFolder + "\\kenshi_ai.ini");

        auto get = [&](const std::string& k, const std::string& def) -> std::string {
            auto it = kv.find(k);
            return (it != kv.end()) ? it->second : def;
        };
        auto getInt = [&](const std::string& k, int def) -> int {
            auto it = kv.find(k);
            return (it != kv.end()) ? std::stoi(it->second) : def;
        };
        auto getFloat = [&](const std::string& k, float def) -> float {
            auto it = kv.find(k);
            return (it != kv.end()) ? std::stof(it->second) : def;
        };

        g_config.serverUrl              = get  ("server_url",           "http://127.0.0.1:9392");
        g_config.timeoutMs              = getInt("timeout_ms",           30000);
        g_config.defaultModel           = get  ("default_model",         "");
        g_config.campaignId             = get  ("campaign_id",           "Default");
        g_config.talkRadius             = getInt("talk_radius",          100);
        g_config.proximityRadius        = getInt("proximity_radius",     200);
        g_config.radiantDelayS          = getFloat("radiant_delay_s",    240.0f);
        g_config.synthesisIntervalMin   = getFloat("synthesis_interval_minutes", 5.0f);
    }

    // Parse the JSON response from the sidecar into a ParsedResponse struct.
    // Avoids pulling in a JSON library — basic substring scanning.
    ParsedResponse ParseResponse(const std::string& json)
    {
        ParsedResponse r;

        // Extract a quoted string value by key from the root JSON object.
        auto extractStr = [&](const std::string& key) -> std::string {
            auto pos = json.find("\"" + key + "\":");
            if (pos == std::string::npos) return "";
            pos = json.find('"', pos + key.size() + 3);
            if (pos == std::string::npos) return "";
            auto end = json.find('"', pos + 1);
            return (end == std::string::npos) ? "" : json.substr(pos + 1, end - pos - 1);
        };

        // Extract an integer value by key from the root JSON object.
        auto extractInt = [&](const std::string& key) -> int {
            auto pos = json.find("\"" + key + "\":");
            if (pos == std::string::npos) return 0;
            pos += key.size() + 3;
            while (pos < json.size() && (json[pos]==' '||json[pos]=='\t')) ++pos;
            if (pos >= json.size()) return 0;
            bool neg = (json[pos] == '-'); if (neg) ++pos;
            int val = 0;
            while (pos < json.size() && std::isdigit((unsigned char)json[pos]))
                val = val * 10 + (json[pos++] - '0');
            return neg ? -val : val;
        };

        r.npcId       = extractStr("npc_id");
        r.opinionAfter = extractInt("opinion_after");

        // Scan actions array — each action object starts after a "kind" key.
        auto actStart = json.find("\"actions\"");
        if (actStart != std::string::npos)
        {
            const std::string sub = json.substr(actStart);

            // Within sub, extract a quoted string by key starting near `from`.
            auto localStr = [&](size_t from, const std::string& key) -> std::string {
                auto p = sub.find("\"" + key + "\":", from);
                if (p == std::string::npos || p > from + 300) return "";
                auto qs = sub.find('"', p + key.size() + 3);
                auto qe = sub.find('"', qs + 1);
                if (qs == std::string::npos || qe == std::string::npos) return "";
                return sub.substr(qs + 1, qe - qs - 1);
            };

            // Within sub, extract an integer by key starting near `from`.
            auto localInt = [&](size_t from, const std::string& key) -> int {
                auto p = sub.find("\"" + key + "\":", from);
                if (p == std::string::npos || p > from + 300) return 0;
                p += key.size() + 3;
                while (p < sub.size() && (sub[p]==' '||sub[p]=='\t')) ++p;
                if (p >= sub.size()) return 0;
                bool neg = (sub[p] == '-'); if (neg) ++p;
                int val = 0;
                while (p < sub.size() && std::isdigit((unsigned char)sub[p]))
                    val = val * 10 + (sub[p++] - '0');
                return neg ? -val : val;
            };

            size_t pos = 0;
            while ((pos = sub.find("\"kind\"", pos)) != std::string::npos)
            {
                auto vstart = sub.find('"', pos + 7);
                auto vend   = sub.find('"', vstart + 1);
                if (vstart == std::string::npos || vend == std::string::npos) break;
                std::string kind = sub.substr(vstart + 1, vend - vstart - 1);

                if (kind == "speak" && r.speakText.empty())
                {
                    r.speakText = localStr(pos, "text");
                }
                else if (kind == "recruit_accept")  r.recruitAccept  = true;
                else if (kind == "recruit_decline")  r.recruitDecline = true;
                else if (kind == "follow")           r.follow         = true;
                else if (kind == "idle")             r.idle           = true;
                else if (kind == "flee")             r.flee           = true;
                else if (kind == "call_guards")      r.callGuards     = true;
                else if (kind == "attack_target") {
                    r.attackTarget   = true;
                    r.attackTargetId = localStr(pos, "target_npc_id");
                }
                else if (kind == "give_item") {
                    r.giveItemName = localStr(pos, "item");
                    r.giveItemQty  = localInt(pos, "quantity");
                    if (r.giveItemQty < 1) r.giveItemQty = 1;
                }
                else if (kind == "take_item") {
                    r.takeItemName = localStr(pos, "item");
                    r.takeItemQty  = localInt(pos, "quantity");
                    if (r.takeItemQty < 1) r.takeItemQty = 1;
                }
                else if (kind == "transfer_cats") {
                    r.transferCats = localInt(pos, "amount");
                }
                else if (kind == "opinion_delta") {
                    r.opinionDelta = localInt(pos, "delta");
                }
                else if (kind == "faction_relation_delta") {
                    r.factionRelDelta  = localInt(pos, "delta");
                    r.factionRelTarget = localStr(pos, "faction");
                }

                pos = vend + 1;
            }
        }

        return r;
    }

    // ---------- Opinion cache ------------------------------------------------

    static std::unordered_map<uintptr_t, int> s_opinionMap;

    int GetOpinion(uintptr_t npcAddr)
    {
        auto it = s_opinionMap.find(npcAddr);
        return (it != s_opinionMap.end()) ? it->second : 0;
    }

    void SetOpinion(uintptr_t npcAddr, int value)
    {
        s_opinionMap[npcAddr] = std::max(-100, std::min(100, value));
    }
}
