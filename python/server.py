"""kenshi-ai sidecar — FastAPI server.

Listens on 127.0.0.1:9392 by default. The native C++ hook (kenshi_ai.dll)
posts chat turns here. Can also be driven directly via curl for development
without Kenshi running.

Layout:
  POST /chat        — one player→NPC turn, returns ChatResponse
  POST /d2d         — NPC↔NPC ambient turn, returns D2DResponse
  GET  /healthz     — liveness + telemetry snapshot
"""

from __future__ import annotations

import asyncio
import logging
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException

from agents.chat_agent import ChatAgent
from agents.d2d_agent import D2DAgent
from agents.synthesis_agent import SynthesisAgent
from memory.chroma_memory import NPCMemory
from providers.router import ProviderRouter
from schemas.actions import ChatRequest, ChatResponse, D2DRequest, D2DResponse

logging.basicConfig(
    level=os.environ.get("KENSHI_AI_LOG", "INFO"),
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)
log = logging.getLogger("kenshi-ai")


async def _synthesis_loop(agent: SynthesisAgent, interval_s: float) -> None:
    while True:
        await asyncio.sleep(interval_s)
        try:
            await agent.run_once(interval_minutes=interval_s / 60.0)
        except Exception:
            log.exception("synthesis loop error")


@asynccontextmanager
async def lifespan(app: FastAPI):
    router = ProviderRouter.from_config_dir()
    await router.__aenter__()

    campaign_id = os.environ.get("KENSHI_CAMPAIGN", "Default")
    memory = NPCMemory(campaign_id=campaign_id)
    await memory.connect()

    default_model = os.environ.get("KENSHI_DEFAULT_MODEL") or next(iter(router.models), "")
    if not default_model:
        raise RuntimeError(
            "No models configured. Populate python/config/models.json "
            "or set KENSHI_DEFAULT_MODEL."
        )

    app.state.router  = router
    app.state.memory  = memory
    app.state.agent   = ChatAgent(router, memory, default_model)
    app.state.d2d     = D2DAgent(router, memory, default_model)
    app.state.synth   = SynthesisAgent(router, memory, default_model)

    interval_s = float(os.environ.get("KENSHI_SYNTHESIS_INTERVAL_S", "300"))
    task = asyncio.create_task(_synthesis_loop(app.state.synth, interval_s))

    log.info("kenshi-ai sidecar ready. model=%s campaign=%s synthesis_interval=%ds",
             default_model, campaign_id, int(interval_s))
    try:
        yield
    finally:
        task.cancel()
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
        "campaign": app.state.memory.campaign_id,
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


@app.post("/d2d", response_model=D2DResponse)
async def d2d(req: D2DRequest) -> D2DResponse:
    try:
        return await app.state.d2d.respond(req)
    except Exception as exc:
        log.exception("d2d failed npc_a=%s npc_b=%s", req.npc_a_id, req.npc_b_id)
        raise HTTPException(status_code=500, detail=repr(exc))
