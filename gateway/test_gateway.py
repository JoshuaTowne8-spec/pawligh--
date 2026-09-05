import json
import os
import sys

from dotenv import load_dotenv
from websockets.exceptions import ConnectionClosed
from websockets.sync.client import connect


load_dotenv(os.path.join(os.path.dirname(__file__), ".env"))
token = os.environ["DEVICE_TOKEN"]
gateway_url = (
    sys.argv[1]
    if len(sys.argv) > 1
    else "ws://127.0.0.1:8080/v1/device/audio"
)

events: list[str] = []
with connect(
    gateway_url,
    additional_headers={"Authorization": f"Bearer {token}"},
    open_timeout=10,
    close_timeout=5,
) as websocket:
    ready = json.loads(websocket.recv(timeout=20))
    events.append(ready.get("type", "unknown"))
    if ready.get("type") != "ready" or ready.get("sample_rate") != 16000:
        raise RuntimeError(f"Gateway wasn't ready: {ready}")

    silence_40ms = bytes(1280)
    for _ in range(10):
        websocket.send(silence_40ms)
    websocket.send(json.dumps({"type": "finish"}))

    try:
        while True:
            response = json.loads(websocket.recv(timeout=5))
            events.append(response.get("type", "unknown"))
    except (ConnectionClosed, TimeoutError):
        pass

print(json.dumps({"gateway_relay": "ok", "events": events}, ensure_ascii=False))
