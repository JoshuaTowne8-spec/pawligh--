import asyncio
import base64
import json
import os
import uuid
from contextlib import suppress
from pathlib import Path

import websockets
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect, status
from fastapi.responses import FileResponse

from .effects import (
    EMOTION_LABELS,
    PRESET_ORDER,
    load_effects,
    save_effects,
    validate_effect,
)


app = FastAPI(title="Pet Emotion Gateway", version="0.2.0")
APP_DIR = Path(__file__).resolve().parent
EFFECTS_PATH = APP_DIR.parent / "data" / "effects.json"
effects = load_effects(EFFECTS_PATH)
connected_devices: set[WebSocket] = set()
device_locks: dict[WebSocket, asyncio.Lock] = {}
active_preset = "calm"

def normalize_emotion(raw: str, transcript: str) -> str:
    """Map ASR's seven acoustic labels plus speech content to five product labels."""
    text = transcript or ""
    if any(word in text for word in ("想你", "想念", "怀念", "回忆", "以前", "离开", "再见", "陪伴")):
        return "miss"
    if any(word in text for word in ("爱你", "谢谢", "乖", "温柔", "拥抱", "好暖", "陪着")):
        return "warm"
    return {
        "happy": "happy",
        "surprised": "happy",
        "neutral": "calm",
        "sad": "sad",
        "angry": "sad",
        "fearful": "sad",
        "disgusted": "sad",
    }.get(raw, "calm")


async def send_to_device(device: WebSocket, payload: dict) -> None:
    lock = device_locks.setdefault(device, asyncio.Lock())
    async with lock:
        await device.send_json(payload)


async def broadcast(payload: dict) -> None:
    stale = []
    for device in tuple(connected_devices):
        try:
            await send_to_device(device, payload)
        except Exception:
            stale.append(device)
    for device in stale:
        connected_devices.discard(device)
        device_locks.pop(device, None)


async def select_preset(preset: str) -> None:
    global active_preset
    active_preset = preset
    await broadcast({"type": "effect.select", "preset": preset, "config": effects[preset]})


def required_env(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise RuntimeError(f"Missing required environment variable: {name}")
    return value


def bearer_token(websocket: WebSocket) -> str:
    value = websocket.headers.get("authorization", "")
    prefix = "Bearer "
    return value[len(prefix) :] if value.startswith(prefix) else ""


def dashscope_url() -> str:
    api_host = required_env("DASHSCOPE_API_HOST")
    api_host = api_host.removeprefix("https://").removeprefix("http://").rstrip("/")
    model = os.getenv("DASHSCOPE_MODEL", "qwen3-asr-flash-realtime")
    return f"wss://{api_host}/api-ws/v1/realtime?model={model}"


def event(event_type: str, **payload: object) -> str:
    return json.dumps(
        {"event_id": f"event_{uuid.uuid4().hex}", "type": event_type, **payload},
        ensure_ascii=False,
        separators=(",", ":"),
    )


def cloud_error(message: dict) -> RuntimeError | None:
    if message.get("type") == "error":
        error = message.get("error") or {}
        return RuntimeError(
            f"{error.get('code', 'unknown')}: {error.get('message', 'Cloud error')}"
        )
    if message.get("code"):
        return RuntimeError(
            f"{message.get('code', 'unknown')}: {message.get('message', 'Cloud error')}"
        )
    return None


async def configure_cloud(cloud: websockets.WebSocketClientProtocol) -> None:
    first = json.loads(await asyncio.wait_for(cloud.recv(), timeout=15))
    if error := cloud_error(first):
        raise error
    if first.get("type") != "session.created":
        raise RuntimeError(f"Unexpected cloud event: {first.get('type', 'unknown')}")

    await cloud.send(
        event(
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

    for _ in range(4):
        message = json.loads(await asyncio.wait_for(cloud.recv(), timeout=15))
        if error := cloud_error(message):
            raise error
        if message.get("type") == "session.updated":
            return
    raise RuntimeError("Cloud session wasn't updated")


@app.get("/healthz")
async def healthz() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/control")
async def control_page():
    return FileResponse(APP_DIR / "static" / "control.html")


@app.get("/api/effects")
async def get_effects() -> dict:
    return {"order": PRESET_ORDER, "labels": EMOTION_LABELS, "effects": effects, "active": active_preset}


@app.get("/api/status")
async def get_status() -> dict:
    return {"devices": len(connected_devices), "active": active_preset}


@app.put("/api/effects/{preset}")
async def update_effect(preset: str, payload: dict) -> dict:
    try:
        effects[preset] = validate_effect(payload, preset)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    save_effects(EFFECTS_PATH, effects)
    if active_preset == preset:
        await select_preset(preset)
    return effects[preset]


@app.post("/api/effects/{preset}/activate")
async def activate_effect(preset: str) -> dict[str, str]:
    if preset not in PRESET_ORDER:
        raise HTTPException(status_code=404, detail="unknown preset")
    await select_preset(preset)
    return {"active": preset}


@app.post("/api/effects/reset")
async def reset_effects() -> dict:
    global effects
    effects = load_effects(Path("__missing_defaults_only__"))
    save_effects(EFFECTS_PATH, effects)
    await select_preset(active_preset)
    return {"effects": effects}


async def send_audio_to_cloud(device: WebSocket, cloud: websockets.WebSocketClientProtocol) -> None:
    while True:
        message = await device.receive()
        if message["type"] == "websocket.disconnect":
            return
        audio = message.get("bytes")
        if audio:
            await cloud.send(
                event(
                    "input_audio_buffer.append",
                    audio=base64.b64encode(audio).decode("ascii"),
                )
            )
            continue

        text = message.get("text")
        if not text:
            continue
        command = json.loads(text)
        if command.get("type") == "finish":
            await cloud.send(event("session.finish"))
            return


async def send_results_to_device(device: WebSocket, cloud: websockets.WebSocketClientProtocol) -> None:
    async for raw in cloud:
        message = json.loads(raw)
        message_type = message.get("type", "")

        if error := cloud_error(message):
            await device.send_json(
                {"type": "cloud_error", "message": str(error)}
            )
            return

        if message_type == "conversation.item.input_audio_transcription.completed":
            raw_emotion = message.get("emotion") or "neutral"
            transcript = message.get("transcript", "")
            emotion = normalize_emotion(raw_emotion, transcript)
            await send_to_device(
                device,
                {
                    "type": "effect.select",
                    "emotion": emotion,
                    "preset": emotion,
                    "config": effects[emotion],
                    "raw_emotion": raw_emotion,
                    "transcript": transcript,
                    "hold_ms": 12000,
                }
            )
            global active_preset
            active_preset = emotion
        elif message_type == "session.finished":
            return


@app.websocket("/v1/device/audio")
async def device_audio(websocket: WebSocket) -> None:
    expected_token = required_env("DEVICE_TOKEN")
    if bearer_token(websocket) != expected_token:
        await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
        return

    await websocket.accept()
    connected_devices.add(websocket)
    device_locks.setdefault(websocket, asyncio.Lock())
    api_key = required_env("DASHSCOPE_API_KEY")

    try:
        async with websockets.connect(
            dashscope_url(),
            extra_headers={"Authorization": f"Bearer {api_key}"},
            open_timeout=15,
            ping_interval=20,
            ping_timeout=20,
            max_size=4 * 1024 * 1024,
        ) as cloud:
            await configure_cloud(cloud)
            await send_to_device(websocket, {"type": "ready", "sample_rate": 16000})
            await send_to_device(websocket, {"type": "effects.sync", "presets": effects})
            await send_to_device(websocket, {"type": "effect.select", "preset": active_preset, "config": effects[active_preset]})

            uplink = asyncio.create_task(send_audio_to_cloud(websocket, cloud))
            downlink = asyncio.create_task(send_results_to_device(websocket, cloud))
            done, pending = await asyncio.wait(
                {uplink, downlink}, return_when=asyncio.FIRST_COMPLETED
            )
            for task in pending:
                task.cancel()
            for task in pending:
                with suppress(asyncio.CancelledError):
                    await task
            for task in done:
                task.result()
    except WebSocketDisconnect:
        return
    except Exception as exc:
        with suppress(Exception):
            await send_to_device(
                websocket,
                {"type": "gateway_error", "message": type(exc).__name__}
            )
        with suppress(Exception):
            await websocket.close(code=status.WS_1011_INTERNAL_ERROR)
    finally:
        connected_devices.discard(websocket)
        device_locks.pop(websocket, None)
