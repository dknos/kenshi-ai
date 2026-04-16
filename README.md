# kenshi-ai

LLM-driven NPC dialogue for **Kenshi** — persistent per-NPC memory, structured actions that actually fire in-engine, multi-provider support, world synthesis rumor loop.

Sibling to [`dknos/morrowind-ai`](https://github.com/dknos/morrowind-ai) (OpenMW) and [`dknos/dwarf-ai`](https://github.com/dknos/dwarf-ai) (Dwarf Fortress).

## Status

**Alpha.** Python sidecar fully functional. Native RE_Kenshi C++ plugin scaffolded and compilable (requires Windows + MSVC to build).

## Features

- **Words carry real consequences** — structured Pydantic actions (`give_item`, `recruit_accept`, `attack_target`, `call_guards`, `flee`) fire in-engine when the LLM commits to them
- **Persistent memory** — ChromaDB stores every player↔NPC conversation turn per campaign; NPCs remember you across sessions
- **World synthesis** — background loop distills conversation events into faction rumors that spread through NPC dialogue
- **NPC-to-NPC chatter** — ambient D2D (dialogue-to-dialogue) exchanges between nearby NPCs, also stored in memory
- **Canon lock** — race/faction injected from game state; the LLM cannot overwrite them
- **Silent animals** — Garru, Goat, Bonedog etc. return `...` with zero LLM calls
- **Multi-provider** — xAI Grok, OpenRouter, Anthropic, Gemini, Ollama, LM Studio, llama.cpp; no per-provider lock-in
- **MIT licensed** — no energy/credits/Player2 wrapper

## Architecture

```
Kenshi ─► RE_Kenshi ─► kenshi_ai.dll (MSVC x64)
                              │
                    WinHTTP async POST :9392
                              │
                    Python sidecar (FastAPI)
                              │
             ┌────────────────┼──────────────────┐
             ▼                ▼                   ▼
        chat_agent       d2d_agent        synthesis_agent
             │                                    │
        ┌────┴────┐                         ChromaDB rumors
        ▼         ▼
   Providers   ChromaDB
  (xAI, OAI,  (per-NPC turns,
   OpenRouter,  per-campaign)
   Anthropic,
   Gemini,
   Ollama)
```

## Quick start

### Python sidecar

```bash
cd python
pip install -r requirements.txt
cp config/providers.json.example config/providers.json
# add your API key to providers.json
uvicorn server:app --host 127.0.0.1 --port 9392
```

Test without Kenshi:

```bash
curl -X POST http://127.0.0.1:9392/chat \
  -H "Content-Type: application/json" \
  -d '{"npc_id":"test_beep","npc_name":"Beep","race":"Hiver","faction":"Hivers","message":"Hello."}'
```

### Native DLL (Windows only)

See [docs/BUILD.md](docs/BUILD.md) for full instructions. Summary:

```powershell
cd native
git clone -b RE_Kenshi_mods https://github.com/KenshiReclaimer/KenshiLib deps/KenshiLib
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release `
  -DKENSHI_MOD_OUTPUT_DIR="C:\path\to\Kenshi\mods\kenshi-ai"
cmake --build build --config Release
```

## Configuration

Copy `mod/config/kenshi_ai.ini.example` to your mod folder as `kenshi_ai.ini`:

```ini
[Settings]
server_url              = http://127.0.0.1:9392
timeout_ms              = 30000
default_model           = Grok-4-Fast
campaign_id             = Default
talk_radius             = 100
proximity_radius        = 200
radiant_delay_s         = 240
synthesis_interval_minutes = 5
```

## Mod folder layout

```
Kenshi/mods/kenshi-ai/
  kenshi-ai.json     RE_Kenshi plugin manifest
  kenshi_ai.dll      built by CMake
  kenshi_ai.ini      copied from config/kenshi_ai.ini.example + edited
  python/            sidecar (run separately)
```

## Supported providers

| Key in providers.json | Notes |
|---|---|
| `openrouter` | Recommended — routes to any model |
| `xai` | Direct Grok API |
| `anthropic` | Claude models |
| `gemini` | Google Gemini |
| `ollama` | Local, no key needed |
| `lmstudio` | Local OpenAI-compat server |

## Actions implemented

| Action | Engine effect |
|---|---|
| `speak` | `Dialogue::say()` speech bubble |
| `recruit_accept` | `PlayerInterface::recruit()` |
| `follow` | `_doActions(DA_FOLLOW_WHILE_TALKING)` via RVA |
| `idle` | `_doActions(DA_CLEAR_AI)` via RVA |
| `flee` | `sendEventOverride(EV_SQUAD_BROKEN)` |
| `call_guards` | `sendEventOverride(EV_SOUND_THE_ALARM)` |
| `attack_target` | `sendEventOverride(EV_LAUNCH_ATTACK)` |
| `transfer_cats` | `_doActions(DA_GIVE/TAKE_MONEY)` via RVA |
| `give_item` | NPC `Inventory` → player `Inventory` transfer |
| `take_item` | player `Inventory` → NPC `Inventory` transfer |
| `opinion_delta` | session opinion cache, sent each turn |
| `faction_relation_delta` | `FactionRelations::affectRelations()` |
| `recruit_decline` | speech only |

## Runtime dependencies (not redistributed)

- [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) — script extender
- [KenshiLib](https://github.com/KenshiReclaimer/KenshiLib) — reverse-engineered headers

## License

MIT. See [LICENSE](LICENSE).
