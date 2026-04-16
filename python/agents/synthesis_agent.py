"""World synthesis agent — periodic rumor distillation.

Runs every `interval_minutes` (default 5). Pulls recent NPC turns from
ChromaDB, hands them to the LLM with the world_synthesis.txt prompt, stores
new rumors, retires contradicted ones.
"""

from __future__ import annotations

import json
import logging
import time
from pathlib import Path

from memory.chroma_memory import NPCMemory
from providers.router import ProviderRouter

log = logging.getLogger(__name__)

PROMPT_DIR = Path(__file__).resolve().parent.parent / "prompts"


class SynthesisAgent:
    def __init__(self, router: ProviderRouter, memory: NPCMemory, default_model: str):
        self.router = router
        self.memory = memory
        self.default_model = default_model
        self._tmpl = (PROMPT_DIR / "world_synthesis.txt").read_text()

    async def run_once(self, interval_minutes: float = 5.0) -> None:
        recent = await self.memory.recent_turns(limit=40)
        if not recent:
            return

        events_block = "\n".join(
            f"- [{t.role} @ {t.npc_id}] {t.text}" for t in recent
        )
        existing = await self.memory.list_rumors(limit=20)
        rumors_block = "\n".join(
            f"- [{r['id']}] {r['text']}" for r in existing
        ) or "(none)"

        prompt = (
            self._tmpl
            .replace("{interval_minutes}", str(int(interval_minutes)))
            .replace("{events}", events_block)
            .replace("{existing_rumors}", rumors_block)
        )

        messages = [
            {"role": "system", "content": "You are the world narrator of Kenshi."},
            {"role": "user",   "content": prompt},
        ]
        try:
            data = await self.router.chat(
                self.default_model, messages,
                response_format={"type": "json_object"},
                temperature=0.8,
                max_tokens=512,
            )
            content = data["choices"][0]["message"].get("content", "")
            clean = content.strip()
            if clean.startswith("```"):
                clean = clean.split("\n", 1)[-1].rsplit("```", 1)[0].strip()
            payload = json.loads(clean)
        except Exception as exc:
            log.warning("synthesis failed: %s", exc)
            return

        for r in payload.get("new_rumors", []):
            await self.memory.add_rumor(r.get("text", ""), r.get("tags", []))

        for rid in payload.get("retire_rumor_ids", []):
            await self.memory.retire_rumor(rid)

        log.info("synthesis: +%d rumors, -%d retired",
                 len(payload.get("new_rumors", [])),
                 len(payload.get("retire_rumor_ids", [])))
