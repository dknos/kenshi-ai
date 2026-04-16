"""Pydantic action schema.

Structured JSON tool-call-style actions validated by Pydantic before the hook
sees them. Unparseable / unknown / out-of-range actions are rejected, not
half-applied.
"""

from __future__ import annotations

from typing import Annotated, Literal, Union

from pydantic import BaseModel, Field


class Speak(BaseModel):
    kind: Literal["speak"] = "speak"
    text: str = Field(min_length=1, max_length=1200)


class GiveItem(BaseModel):
    kind: Literal["give_item"] = "give_item"
    item: str
    quantity: int = Field(ge=1, le=9999, default=1)


class TakeItem(BaseModel):
    kind: Literal["take_item"] = "take_item"
    item: str
    quantity: int = Field(ge=1, le=9999, default=1)


class TransferCats(BaseModel):
    """Cats = Kenshi's in-game currency."""

    kind: Literal["transfer_cats"] = "transfer_cats"
    amount: int = Field(
        description="Positive = NPC pays player. Negative = player pays NPC."
    )


class Follow(BaseModel):
    kind: Literal["follow"] = "follow"


class Idle(BaseModel):
    kind: Literal["idle"] = "idle"


class RecruitAccept(BaseModel):
    kind: Literal["recruit_accept"] = "recruit_accept"


class RecruitDecline(BaseModel):
    kind: Literal["recruit_decline"] = "recruit_decline"
    reason: str | None = None


class AttackTarget(BaseModel):
    kind: Literal["attack_target"] = "attack_target"
    target_npc_id: str


class Flee(BaseModel):
    kind: Literal["flee"] = "flee"


class CallGuards(BaseModel):
    kind: Literal["call_guards"] = "call_guards"


class OpinionDelta(BaseModel):
    kind: Literal["opinion_delta"] = "opinion_delta"
    delta: int = Field(ge=-100, le=100)


class FactionRelationDelta(BaseModel):
    kind: Literal["faction_relation_delta"] = "faction_relation_delta"
    faction: str
    delta: int = Field(ge=-100, le=100)


class RevealRumor(BaseModel):
    kind: Literal["reveal_rumor"] = "reveal_rumor"
    rumor_id: str


Action = Annotated[
    Union[
        Speak,
        GiveItem,
        TakeItem,
        TransferCats,
        Follow,
        Idle,
        RecruitAccept,
        RecruitDecline,
        AttackTarget,
        Flee,
        CallGuards,
        OpinionDelta,
        FactionRelationDelta,
        RevealRumor,
    ],
    Field(discriminator="kind"),
]


class ChatResponse(BaseModel):
    """Canonical reply from the LLM agent.

    One turn may produce multiple actions; at least one `speak` is typical.
    The hook applies actions in order and renders the first `speak` as a
    speech bubble.
    """

    npc_id: str
    actions: list[Action] = Field(min_length=1)
    opinion_after: int = Field(ge=-100, le=100, default=0)
    faction_relation_after: int = Field(ge=-100, le=100, default=0)


class ChatRequest(BaseModel):
    """What the C++ hook POSTs for every player→NPC turn."""

    npc_id: str
    npc_name: str
    race: str
    faction: str | None = None
    health_frac: float = Field(ge=0.0, le=1.0, default=1.0)
    hunger_frac: float = Field(ge=0.0, le=1.0, default=0.0)
    kos: bool = False
    player_faction: str | None = None
    player_faction_rel: int = Field(ge=-100, le=100, default=0)
    opinion: int = Field(ge=-100, le=100, default=0)
    nearby_npc_ids: list[str] = Field(default_factory=list)
    location: str | None = None
    message: str
    campaign_id: str = "Default"
    model_override: str | None = None


class D2DRequest(BaseModel):
    """NPC-to-NPC ambient turn posted by the DLL radiant loop."""

    npc_a_id: str
    npc_a_name: str
    npc_a_race: str
    npc_a_faction: str | None = None
    npc_b_id: str
    npc_b_name: str
    npc_b_race: str
    npc_b_faction: str | None = None
    location: str | None = None
    campaign_id: str = "Default"
    model_override: str | None = None


class D2DLine(BaseModel):
    speaker_id: str
    text: str


class D2DResponse(BaseModel):
    lines: list[D2DLine] = Field(min_length=1, max_length=4)
