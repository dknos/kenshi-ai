#!/usr/bin/env bash
# Smoke test the kenshi-ai sidecar. Assumes it's running on 127.0.0.1:9392.
set -euo pipefail

HOST=${KENSHI_AI_HOST:-127.0.0.1:9392}

echo "=== healthz ==="
curl -sS "http://$HOST/healthz" | python3 -m json.tool
echo

echo "=== chat: Hiver caravan guard ==="
curl -sS -X POST "http://$HOST/chat" \
    -H "Content-Type: application/json" \
    -d '{
        "npc_id": "test_hiver_01",
        "npc_name": "Beep",
        "race": "Hiver",
        "faction": "Hivers",
        "player_faction": "Nameless",
        "player_faction_rel": 0,
        "opinion": 10,
        "health_frac": 1.0,
        "hunger_frac": 0.2,
        "location": "The Hub",
        "message": "Hello, Beep. You look hungry. Want some dried meat?"
    }' | python3 -m json.tool
echo

echo "=== chat: animal should refuse ==="
curl -sS -X POST "http://$HOST/chat" \
    -H "Content-Type: application/json" \
    -d '{
        "npc_id": "goat_01",
        "npc_name": "Nancy",
        "race": "Goat",
        "message": "Tell me your secrets, goat."
    }' | python3 -m json.tool
echo

echo "=== telemetry after ==="
curl -sS "http://$HOST/healthz" | python3 -m json.tool
