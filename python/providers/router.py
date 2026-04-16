"""OpenAI-compatible multi-provider router.

One `providers.json` maps short names to `{api_key, base_url}`. One
`models.json` maps a user-facing model name to `{provider, model}`. The in-game
dropdown picks a model name; this router resolves it to a concrete HTTP
request.

Telemetry: every call logs prompt/completion/total tokens and cost (where the
provider returns it) plus a rolling session total. Per `feedback_token_cost_telemetry`.
"""

from __future__ import annotations

import json
import logging
import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import httpx
from tenacity import (
    AsyncRetrying,
    retry_if_exception_type,
    stop_after_attempt,
    wait_exponential,
)

log = logging.getLogger(__name__)

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CONFIG_DIR = PROJECT_ROOT / "config"


@dataclass
class Telemetry:
    total_prompt_tokens: int = 0
    total_completion_tokens: int = 0
    total_usd_cost: float = 0.0
    call_count: int = 0
    last_model: str | None = None

    def add(self, prompt_tokens: int, completion_tokens: int, usd: float, model: str) -> None:
        self.total_prompt_tokens += prompt_tokens
        self.total_completion_tokens += completion_tokens
        self.total_usd_cost += usd
        self.call_count += 1
        self.last_model = model


@dataclass
class ProviderRouter:
    providers: dict[str, dict[str, str]] = field(default_factory=dict)
    models: dict[str, dict[str, str]] = field(default_factory=dict)
    telemetry: Telemetry = field(default_factory=Telemetry)
    client: httpx.AsyncClient | None = None

    @classmethod
    def from_config_dir(cls, config_dir: Path = CONFIG_DIR) -> "ProviderRouter":
        providers_file = config_dir / "providers.json"
        models_file = config_dir / "models.json"

        if not providers_file.exists():
            example = config_dir / "providers.json.example"
            raise FileNotFoundError(
                f"Missing {providers_file}. Copy {example} and add your API keys."
            )
        providers = json.loads(providers_file.read_text())
        models = json.loads(models_file.read_text()) if models_file.exists() else {}

        providers = _resolve_env_refs(providers)
        return cls(providers=providers, models=models)

    async def __aenter__(self) -> "ProviderRouter":
        self.client = httpx.AsyncClient(timeout=60.0)
        return self

    async def __aexit__(self, *_: Any) -> None:
        if self.client:
            await self.client.aclose()

    def resolve(self, model_name: str) -> tuple[str, str, dict[str, str]]:
        """Return (provider_name, concrete_model, provider_config)."""
        if model_name not in self.models:
            raise KeyError(f"Unknown model '{model_name}'. Available: {list(self.models)}")
        entry = self.models[model_name]
        provider_name = entry["provider"]
        if provider_name not in self.providers:
            raise KeyError(
                f"Model '{model_name}' references missing provider '{provider_name}'"
            )
        return provider_name, entry["model"], self.providers[provider_name]

    async def chat(
        self,
        model_name: str,
        messages: list[dict[str, str]],
        *,
        response_format: dict[str, Any] | None = None,
        temperature: float = 0.7,
        max_tokens: int = 1024,
    ) -> dict[str, Any]:
        assert self.client is not None, "Use `async with router` first"
        provider_name, concrete_model, provider = self.resolve(model_name)
        url = provider["base_url"].rstrip("/") + "/chat/completions"
        headers = {
            "Authorization": f"Bearer {provider['api_key']}",
            "Content-Type": "application/json",
        }
        payload: dict[str, Any] = {
            "model": concrete_model,
            "messages": messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
        }
        if response_format:
            payload["response_format"] = response_format

        t0 = time.perf_counter()
        async for attempt in AsyncRetrying(
            stop=stop_after_attempt(3),
            wait=wait_exponential(multiplier=1, min=2, max=10),
            retry=retry_if_exception_type((httpx.HTTPError, httpx.TimeoutException)),
            reraise=True,
        ):
            with attempt:
                resp = await self.client.post(url, json=payload, headers=headers)
                resp.raise_for_status()
                data = resp.json()

        latency_ms = int((time.perf_counter() - t0) * 1000)

        usage = data.get("usage", {})
        prompt_toks = int(usage.get("prompt_tokens", 0))
        completion_toks = int(usage.get("completion_tokens", 0))
        usd = _estimate_usd(concrete_model, prompt_toks, completion_toks)
        self.telemetry.add(prompt_toks, completion_toks, usd, concrete_model)

        log.info(
            "llm_call provider=%s model=%s latency_ms=%d prompt_toks=%d "
            "completion_toks=%d usd=%.5f session_total_usd=%.4f",
            provider_name,
            concrete_model,
            latency_ms,
            prompt_toks,
            completion_toks,
            usd,
            self.telemetry.total_usd_cost,
        )
        return data


_PRICE_TABLE_USD_PER_MTOK = {
    # prompt, completion (USD / 1M tokens). Best-effort; update as needed.
    "grok-4": (3.0, 15.0),
    "grok-4-fast": (0.2, 0.5),
    "gpt-4o-mini": (0.15, 0.6),
    "claude-haiku-4-5-20251001": (1.0, 5.0),
    "gemini-3.1-flash-lite-preview": (0.075, 0.3),
}


def _estimate_usd(model: str, prompt: int, completion: int) -> float:
    for key, (p, c) in _PRICE_TABLE_USD_PER_MTOK.items():
        if key in model.lower():
            return (prompt / 1_000_000) * p + (completion / 1_000_000) * c
    return 0.0


def _resolve_env_refs(providers: dict[str, dict[str, str]]) -> dict[str, dict[str, str]]:
    """Replace `$ENV_VAR` api_keys with actual env values so users don't have to
    commit keys to providers.json."""
    out: dict[str, dict[str, str]] = {}
    for name, cfg in providers.items():
        resolved = dict(cfg)
        key = resolved.get("api_key", "")
        if isinstance(key, str) and key.startswith("$"):
            env_name = key.lstrip("$")
            resolved["api_key"] = os.environ.get(env_name, "")
        out[name] = resolved
    return out
