"""Per-NPC episodic memory backed by ChromaDB.

One persistent ChromaDB client per campaign, stored under
`python/campaigns/<campaign_id>/chroma/`. Two collections:
  - `npc_turns`   — every player↔NPC conversation turn
  - `rumors`      — world-synthesis rumor log

ChromaDB handles embeddings internally (default: all-MiniLM-L6-v2).
Recall uses semantic similarity + `where` filter on `npc_id`.

If the DB path isn't writable, falls back to an in-process ephemeral
client so the sidecar keeps running without crashing.
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from pathlib import Path

import chromadb
from chromadb.config import Settings

log = logging.getLogger(__name__)

DATA_ROOT = Path(__file__).resolve().parent.parent / "campaigns"


@dataclass
class Turn:
    npc_id: str
    role: str  # "player" or "npc"
    text: str
    ts: float = field(default_factory=time.time)


class NPCMemory:
    def __init__(self, campaign_id: str = "Default"):
        self.campaign_id = campaign_id
        db_path = DATA_ROOT / campaign_id / "chroma"
        try:
            db_path.mkdir(parents=True, exist_ok=True)
            self._client = chromadb.PersistentClient(
                path=str(db_path),
                settings=Settings(anonymized_telemetry=False),
            )
            log.info("chroma memory at %s", db_path)
        except Exception as exc:
            log.warning("chroma persistent client failed (%s) — using ephemeral", exc)
            self._client = chromadb.EphemeralClient(
                settings=Settings(anonymized_telemetry=False)
            )

        self._turns = self._client.get_or_create_collection(
            "npc_turns",
            metadata={"hnsw:space": "cosine"},
        )
        self._rumors = self._client.get_or_create_collection(
            "rumors",
            metadata={"hnsw:space": "cosine"},
        )

    async def connect(self) -> None:
        pass  # ChromaDB initialises synchronously in __init__

    async def remember(self, turn: Turn) -> None:
        doc_id = f"{turn.npc_id}_{int(turn.ts * 1000)}"
        self._turns.upsert(
            ids=[doc_id],
            documents=[turn.text],
            metadatas=[{"npc_id": turn.npc_id, "role": turn.role, "ts": turn.ts}],
        )

    async def recall(self, npc_id: str, query: str, k: int = 6) -> list[Turn]:
        count = self._turns.count()
        if count == 0:
            return []
        results = self._turns.query(
            query_texts=[query],
            n_results=min(k, count),
            where={"npc_id": npc_id},
        )
        turns: list[Turn] = []
        docs = results.get("documents", [[]])[0]
        metas = results.get("metadatas", [[]])[0]
        for doc, meta in zip(docs, metas):
            turns.append(Turn(npc_id=meta["npc_id"], role=meta["role"], text=doc, ts=meta["ts"]))
        turns.sort(key=lambda t: t.ts)
        return turns

    async def add_rumor(self, text: str, tags: list[str]) -> None:
        doc_id = f"rumor_{int(time.time() * 1000)}"
        self._rumors.upsert(
            ids=[doc_id],
            documents=[text],
            metadatas=[{"tags": ",".join(tags), "ts": time.time()}],
        )
