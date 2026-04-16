"""Per-NPC episodic memory backed by Qdrant.

Talks to the existing :memory-server Qdrant (already running in the workspace).
One collection per game: `kenshi_<campaign>_npc` and `kenshi_<campaign>_rumors`.
Points are keyed by `(npc_id, timestamp)`.

If Qdrant is unreachable, memory falls back to an in-process list so the
sidecar keeps working in single-session mode. The degraded mode is logged at
WARNING level and shouldn't be silent.
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from typing import Any

try:
    from qdrant_client import AsyncQdrantClient
    from qdrant_client.models import Distance, PointStruct, VectorParams
except ImportError:  # optional at import-time for test environments
    AsyncQdrantClient = None  # type: ignore[assignment]

log = logging.getLogger(__name__)

VECTOR_SIZE = 384


@dataclass
class Turn:
    npc_id: str
    role: str  # "player" or "npc"
    text: str
    ts: float = field(default_factory=time.time)


@dataclass
class NPCMemory:
    url: str = "http://127.0.0.1:6333"
    campaign_id: str = "Default"
    embed_fn: Any = None
    _client: Any = None
    _fallback: dict[str, list[Turn]] = field(default_factory=dict)
    _online: bool = False

    @property
    def npc_collection(self) -> str:
        return f"kenshi_{self.campaign_id}_npc"

    @property
    def rumor_collection(self) -> str:
        return f"kenshi_{self.campaign_id}_rumors"

    async def connect(self) -> None:
        if AsyncQdrantClient is None:
            log.warning("qdrant-client not installed; memory running in-process only")
            return
        try:
            self._client = AsyncQdrantClient(url=self.url)
            await self._ensure_collection(self.npc_collection)
            await self._ensure_collection(self.rumor_collection)
            self._online = True
        except Exception as exc:
            log.warning("qdrant unreachable at %s (%s) — in-process fallback", self.url, exc)
            self._online = False

    async def _ensure_collection(self, name: str) -> None:
        collections = await self._client.get_collections()
        names = {c.name for c in collections.collections}
        if name not in names:
            await self._client.create_collection(
                collection_name=name,
                vectors_config=VectorParams(size=VECTOR_SIZE, distance=Distance.COSINE),
            )

    async def remember(self, turn: Turn) -> None:
        if not self._online or self.embed_fn is None:
            self._fallback.setdefault(turn.npc_id, []).append(turn)
            return
        vec = await self.embed_fn(turn.text)
        point = PointStruct(
            id=int(turn.ts * 1000),
            vector=vec,
            payload={
                "npc_id": turn.npc_id,
                "role": turn.role,
                "text": turn.text,
                "ts": turn.ts,
            },
        )
        await self._client.upsert(self.npc_collection, points=[point])

    async def recall(self, npc_id: str, query: str, k: int = 6) -> list[Turn]:
        if not self._online or self.embed_fn is None:
            return self._fallback.get(npc_id, [])[-k:]
        vec = await self.embed_fn(query)
        hits = await self._client.search(
            collection_name=self.npc_collection,
            query_vector=vec,
            limit=k,
            query_filter={"must": [{"key": "npc_id", "match": {"value": npc_id}}]},
        )
        return [
            Turn(
                npc_id=h.payload["npc_id"],
                role=h.payload["role"],
                text=h.payload["text"],
                ts=h.payload["ts"],
            )
            for h in hits
        ]

    async def add_rumor(self, text: str, tags: list[str]) -> None:
        if not self._online or self.embed_fn is None:
            self._fallback.setdefault("__rumors__", []).append(
                Turn(npc_id="__rumors__", role="world", text=text)
            )
            return
        vec = await self.embed_fn(text)
        point = PointStruct(
            id=int(time.time() * 1000),
            vector=vec,
            payload={"text": text, "tags": tags, "ts": time.time()},
        )
        await self._client.upsert(self.rumor_collection, points=[point])
