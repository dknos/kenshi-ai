"""kenshi-ai sidecar — FastAPI server.

Listens on 127.0.0.1:9392 by default. The native C++ hook (kenshi_ai.dll)
posts chat turns here. Can also be driven directly via curl for development
without Kenshi running.

Layout:
  POST /chat        — one player→NPC turn, returns ChatResponse
  POST /d2d         — NPC↔NPC ambient turn (stubbed for v0.1)
  GET  /healthz     — liveness + telemetry snapshot
"""

from __future__ import annotations

import logging
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException

from agents.chat_agent import ChatAgent
from memory.qdrant_memory import NPCMemory
from providers.router import ProviderRouter
from schemas.actions import ChatRequest, ChatResponse

logging.basicConfig(
    level=os.environ.get("KENSHI_AI_LOG", "INFO"),
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)
log = logging.getLogger("kenshi-ai")


@asynccontextmanager
async def lifespan(app: FastAPI):
    router = ProviderRouter.from_config_dir()
    await router.__aenter__()
    memory = NPCMemory(
        url=os.environ.get("QDRANT_URL", "http://127.0.0.1:6333"),
        campaign_id=os.environ.get("KENSHI_CAMPAIGN", "Default"),
    )
    await memory.connect()
    default_model = os.environ.get("KENSHI_DEFAULT_MODEL") or next(iter(router.models), "")
    if not default_model:
        raise RuntimeError(
            "No models configured. Populate python/config/models.json "
            "or set KENSHI_DEFAULT_MODEL."
        )
    app.state.router = router
    app.state.memory = memory
    app.state.agent = ChatAgent(router, memory, default_model)
    log.info("kenshi-ai sidecar ready. default_model=%s qdrant_online=%s",
             default_model, memory._online)
    try:
        yield
    finally:
        await router.__aexit__(None, None, None)


app = FastAPI(title="kenshi-ai sidecar", lifespan=lifespan)


@app.get("/healthz")
async def healthz() -> dict:
    t = app.state.router.telemetry
    return {
        "ok": True,
        "calls": t.call_count,
        "prompt_tokens": t.total_prompt_tokens,
        "completion_tokens": t.total_completion_tokens,
        "usd_total": round(t.total_usd_cost, 5),
        "last_model": t.last_model,
        "qdrant_online": app.state.memory._online,
    }


@app.post("/chat", response_model=ChatResponse)
async def chat(req: ChatRequest) -> ChatResponse:
    try:
        return await app.state.agent.respond(req)
    except KeyError as exc:
        raise HTTPException(status_code=400, detail=str(exc))
    except Exception as exc:
        log.exception("chat failed for npc=%s", req.npc_id)
        raise HTTPException(status_code=500, detail=repr(exc))


@app.post("/d2d")
async def d2d() -> dict:
    return {"ok": False, "reason": "d2d not implemented in v0.1"}
