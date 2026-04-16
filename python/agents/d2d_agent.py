"""NPC-to-NPC ambient dialogue agent (d2d = dialogue-to-dialogue).

The DLL fires POST /d2d when two NPCs are in proximity and the radiant
timer fires. This agent produces 1-4 lines of ambient chatter and stores
them in memory for both participants.
"""

from __future__ import annotations

import json
import logging
import time
from pathlib import Path

from pydantic import ValidationError

from memory.chroma_memory import NPCMemory, Turn
from providers.router import ProviderRouter
from schemas.actions import D2DRequest, D2DResponse, D2DLine

log = logging.getLogger(__name__)

PROMPT_DIR = Path(__file__).resolve().parent.parent / "prompts"


class D2DAgent:
    def __init__(self, router: ProviderRouter, memory: NPCMemory, default_model: str):
        self.router = router
        self.memory = memory
        self.default_model = default_model
        self._tmpl = (PROMPT_DIR / "d2d.txt").read_text()

    async def respond(self, req: D2DRequest) -> D2DResponse:
        mem_a = await self.memory.recall(req.npc_a_id, req.npc_b_name, k=4)
        mem_b = await self.memory.recall(req.npc_b_id, req.npc_a_name, k=4)
        mem_block = ""
        if mem_a or mem_b:
            lines = [f"- [{t.role}@{t.npc_id}] {t.text}" for t in (mem_a + mem_b)]
            mem_block = "\nPAST CONTEXT:\n" + "\n".join(lines)

        prompt = (
            self._tmpl
            .replace("{npc_a_name}", req.npc_a_name)
            .replace("{npc_a_race}", req.npc_a_race)
            .replace("{npc_a_faction}", req.npc_a_faction or "unaffiliated")
            .replace("{npc_b_name}", req.npc_b_name)
            .replace("{npc_b_race}", req.npc_b_race)
            .replace("{npc_b_faction}", req.npc_b_faction or "unaffiliated")
            .replace("{location}", req.location or "the wastes")
            .replace("{memory}", mem_block)
        )

        messages = [
            {"role": "system", "content": prompt},
            {"role": "user",   "content": "Generate the ambient exchange."},
        ]
        model = req.model_override or self.default_model
        try:
            data = await self.router.chat(
                model, messages,
                response_format={"type": "json_object"},
                temperature=0.9,
                max_tokens=400,
            )
            content = data["choices"][0]["message"].get("content", "")
            clean = content.strip()
            if clean.startswith("```"):
                clean = clean.split("\n", 1)[-1].rsplit("```", 1)[0].strip()
            payload = json.loads(clean)
            response = D2DResponse(**payload)
        except (json.JSONDecodeError, ValidationError, Exception) as exc:
            log.warning("d2d parse failed: %s", exc)
            response = D2DResponse(lines=[
                D2DLine(speaker_id=req.npc_a_id, text="..."),
            ])

        ts = time.time()
        for line in response.lines:
            npc_id = line.speaker_id
            await self.memory.remember(Turn(npc_id=npc_id, role="npc", text=line.text, ts=ts))
            ts += 0.001

        return response
