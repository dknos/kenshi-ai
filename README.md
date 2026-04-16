# kenshi-ai

LLM-driven NPC dialogue for **Kenshi** — persistent per-NPC memory, structured actions that actually fire in-engine, multi-provider, works with xAI Grok / OpenRouter / Anthropic / Gemini / Ollama / LM Studio / llama.cpp.

Sibling to [`dknos/morrowind-ai`](https://github.com/dknos/morrowind-ai) (OpenMW) and [`dknos/dwarf-ai`](https://github.com/dknos/dwarf-ai) (Dwarf Fortress).

## Status

**Pre-alpha.** Python sidecar scaffold live; native C++ hook against RE_Kenshi plugin API not yet started.

## Design goals

- **Words carry real consequences.** Structured Pydantic actions — `give_item`, `recruit_accept`, `follow`, `attack_target` — fire in-engine when the LLM commits to them. No more "AI narrates trading an item but nothing happens."
- **Original implementation.** Written from scratch. MIT-licensed, consistent with the series.
- **Canon respected.** Game-known NPCs have a locked profile in `canon.json` — the LLM can't turn Burn into a female Shek elder.
- **No per-provider vendor lock.** Any OpenAI-compatible provider works. No Player2 / energy / credits system.
- **Cross-platform server.** Windows primary target (native hook is Windows-only), but the Python sidecar runs on Linux too.

## Architecture

```
Kenshi (unmodified) ─► RE_Kenshi ─► kenshi_ai.dll (TODO)
                                          │
                                          ▼ HTTP :9392
                                  Python sidecar (FastAPI)
                                          │
                          ┌───────────────┼────────────────┐
                          ▼               ▼                ▼
                     chat_agent     world_synthesis   profile_agent
                          │
                  ┌───────┴────────┐
                  ▼                ▼
              Providers        Qdrant memory
            (xAI, OpenAI,    (per-NPC episodic,
             OpenRouter,     per-campaign rumor log)
             Gemini, Claude,
             Ollama, llama.cpp)
```

## Running the sidecar (development)

```bash
cd python
pip install -r requirements.txt
cp config/providers.json.example config/providers.json
# edit providers.json — add your xAI or OpenRouter key
uvicorn server:app --host 127.0.0.1 --port 9392 --reload
```

Test with curl:

```bash
curl -X POST http://127.0.0.1:9392/chat \
  -H "Content-Type: application/json" \
  -d '{"npc_id":"npc_001","npc_name":"Beep","race":"Hiver","faction":"Hivers","message":"Hello, Beep."}'
```

## License

MIT. See [LICENSE](LICENSE).

## Attributions

Depends on [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) and [KenshiLib](https://github.com/KenshiReclaimer/KenshiLib) as external runtime dependencies (not redistributed).
