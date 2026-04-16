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

        auto extractStr = [&](const std::string& key) -> std::string {
            auto pos = json.find("\"" + key + "\":");
            if (pos == std::string::npos) return "";
            pos = json.find('"', pos + key.size() + 3);
            if (pos == std::string::npos) return "";
            auto end = json.find('"', pos + 1);
            return (end == std::string::npos) ? "" : json.substr(pos + 1, end - pos - 1);
        };

        r.npcId = extractStr("npc_id");

        // Scan actions array
        auto actStart = json.find("\"actions\"");
        if (actStart != std::string::npos)
        {
            std::string sub = json.substr(actStart);
            size_t pos = 0;
            while ((pos = sub.find("\"kind\"", pos)) != std::string::npos)
            {
                auto vstart = sub.find('"', pos + 7);
                auto vend   = sub.find('"', vstart + 1);
                if (vstart == std::string::npos || vend == std::string::npos) break;
                std::string kind = sub.substr(vstart + 1, vend - vstart - 1);

                if (kind == "speak" && r.speakText.empty())
                {
                    auto ts = sub.find("\"text\"", pos);
                    if (ts != std::string::npos) {
                        auto qs = sub.find('"', ts + 7);
                        auto qe = sub.find('"', qs + 1);
                        if (qs != std::string::npos && qe != std::string::npos)
                            r.speakText = sub.substr(qs + 1, qe - qs - 1);
                    }
                }
                else if (kind == "recruit_accept")  r.recruitAccept  = true;
                else if (kind == "recruit_decline")  r.recruitDecline = true;
                else if (kind == "follow")           r.follow         = true;
                else if (kind == "idle")             r.idle           = true;
                else if (kind == "flee")             r.flee           = true;
                else if (kind == "call_guards")      r.callGuards     = true;
                else if (kind == "attack_target") {
                    r.attackTarget   = true;
                    r.attackTargetId = extractStr("target_npc_id");
                }

                pos = vend + 1;
            }
        }

        return r;
    }
}
