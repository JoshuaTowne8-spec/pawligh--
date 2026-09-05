import json
import os
import uuid

from dotenv import load_dotenv
from websockets.exceptions import ConnectionClosed
from websockets.sync.client import connect


load_dotenv(os.path.join(os.path.dirname(__file__), ".env"))

api_key = os.environ["DASHSCOPE_API_KEY"]
api_host = os.environ["DASHSCOPE_API_HOST"].removeprefix("https://").rstrip("/")
model = os.getenv("DASHSCOPE_MODEL", "qwen3-asr-flash-realtime")
url = f"wss://{api_host}/api-ws/v1/realtime?model={model}"


def new_event(event_type: str, **payload: object) -> str:
    return json.dumps(
        {"event_id": f"event_{uuid.uuid4().hex}", "type": event_type, **payload},
        ensure_ascii=False,
        separators=(",", ":"),
    )


seen: list[str] = []
try:
    with connect(
        url,
        additional_headers={"Authorization": f"Bearer {api_key}"},
        open_timeout=15,
        close_timeout=5,
    ) as websocket:
        first = json.loads(websocket.recv(timeout=15))
        seen.append(first.get("type", "unknown"))
        if first.get("type") == "error":
            error = first.get("error", {})
            raise RuntimeError(f"{error.get('code', 'unknown')}: {error.get('message', 'Cloud error')}")

        websocket.send(
            new_event(
                "session.update",
                session={
                    "input_audio_format": "pcm",
                    "sample_rate": 16000,
                    "input_audio_transcription": {"language": "zh"},
                    "turn_detection": {
                        "type": "server_vad",
                        "threshold": 0.2,
                        "silence_duration_ms": 500,
                    },
                },
            )
        )

        for _ in range(6):
            message = json.loads(websocket.recv(timeout=15))
            message_type = message.get("type", "unknown")
            seen.append(message_type)
            if message_type == "unknown":
                print(json.dumps({"unknown_event": message}, ensure_ascii=False))
            if message_type == "error":
                error = message.get("error", {})
                raise RuntimeError(f"{error.get('code', 'unknown')}: {error.get('message', 'Cloud error')}")
            if message_type == "session.updated":
                websocket.send(new_event("session.finish"))
            if message_type == "session.finished":
                break
except ConnectionClosed as exc:
    print(json.dumps({"cloud_connection": "closed", "events": seen, "close_code": exc.code}, ensure_ascii=False))
    raise

print(json.dumps({"cloud_connection": "ok", "events": seen}, ensure_ascii=False))
