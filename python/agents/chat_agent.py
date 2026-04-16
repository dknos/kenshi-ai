"""Chat agent — one player-to-NPC turn.

Composes a prompt from: system template + canon lock + memory recall + request
context, calls the provider router, validates the result against the
ChatResponse schema, and returns it.

Key invariants:
- Race/faction come from game state (canon-locked); the LLM cannot overwrite.
- Animals get an empty-speak bypass (no LLM call at all — fixes "my goat kept
  telling me about her travelling friend").
- Empty/invalid responses retry once with a temperature bump before giving up.
"""

from __future__ import annotations

import json
import logging
from pathlib import Path

from pydantic import ValidationError

from memory.qdrant_memory import NPCMemory, Turn
from providers.router import ProviderRouter
from schemas.actions import ChatRequest, ChatResponse, Speak

log = logging.getLogger(__name__)

PROMPT_DIR = Path(__file__).resolve().parent.parent / "prompts"

SILENT_RACES = {
    "Garru",
    "Goat",
    "Dog",
    "Bonedog",
    "Beak Thing",
    "Crab",
    "Leviathan",
    "Bull",
    "Pack Bull",
    "Spider",
    "Iron Spider",
    "Swamp Turtle",
    "Wild Bull",
    "River Raptor",
    "Skimmer",
    "Gorillo",
}


class ChatAgent:
    def __init__(self, router: ProviderRouter, memory: NPCMemory, default_model: str):
        self.router = router
        self.memory = memory
        self.default_model = default_model
        self._system_tmpl = (PROMPT_DIR / "system_npc.txt").read_text()

    async def respond(self, req: ChatRequest) -> ChatResponse:
        if req.race in SILENT_RACES:
            return ChatResponse(
                npc_id=req.npc_id,
                actions=[Speak(text="...")],
                opinion_after=req.opinion,
                faction_relation_after=req.player_faction_rel,
            )

        recalled = await self.memory.recall(req.npc_id, req.message, k=6)
        memory_block = "\n".join(f"- [{t.role}] {t.text}" for t in recalled) or "(none)"
        subs = {
            "{npc_name}": req.npc_name,
            "{race}": req.race,
            "{faction}": req.faction or "unaffiliated",
            "{player_faction_rel}": str(req.player_faction_rel),
            "{opinion}": str(req.opinion),
            "{health_frac}": f"{req.health_frac:.0%}",
            "{hunger_frac}": f"{req.hunger_frac:.0%}",
        }
        system = self._system_tmpl
        for placeholder, value in subs.items():
            system = system.replace(placeholder, value)
        system += f"\n\nMEMORY:\n{memory_block}\n"

        messages = [
            {"role": "system", "content": system},
            {"role": "user", "content": req.message},
        ]

        model_name = req.model_override or self.default_model
        response = await self._call_and_parse(model_name, messages, req.npc_id, temperature=0.7)

        if response is None or not response.actions:
            log.warning("empty/invalid response from %s; retrying hot", model_name)
            response = await self._call_and_parse(
                model_name, messages, req.npc_id, temperature=1.0
            )
            if response is None:
                response = ChatResponse(
                    npc_id=req.npc_id,
                    actions=[Speak(text="...")],
                    opinion_after=req.opinion,
                    faction_relation_after=req.player_faction_rel,
                )

        await self.memory.remember(Turn(npc_id=req.npc_id, role="player", text=req.message))
        for a in response.actions:
            if a.kind == "speak":
                await self.memory.remember(
                    Turn(npc_id=req.npc_id, role="npc", text=a.text)
                )
        return response

    async def _call_and_parse(
        self,
        model_name: str,
        messages: list[dict[str, str]],
        npc_id: str,
        *,
        temperature: float,
    ) -> ChatResponse | None:
        data = await self.router.chat(
            model_name,
            messages,
            response_format={"type": "json_object"},
            temperature=temperature,
            max_tokens=1024,
        )
        content = data["choices"][0]["message"].get("content", "")
        if not content.strip():
            return None
        try:
            # Strip markdown fences some providers add despite json_object mode
            clean = content.strip()
            if clean.startswith("```"):
                clean = clean.split("\n", 1)[-1]
                clean = clean.rsplit("```", 1)[0].strip()
            payload = json.loads(clean)
            payload["npc_id"] = npc_id  # always enforce — model shouldn't generate this
            return ChatResponse(**payload)
        except (json.JSONDecodeError, ValidationError) as exc:
            log.warning("response parse failed: %s — content=%r", exc, content[:200])
            return None
