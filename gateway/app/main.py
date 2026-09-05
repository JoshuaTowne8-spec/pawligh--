import asyncio
import base64
import json
import os
import uuid
from contextlib import suppress

import websockets
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, status


app = FastAPI(title="Pet Emotion Gateway", version="0.1.0")

EMOTION_TO_EFFECT = {
    "neutral": "calm_breath",
    "happy": "warm_sparkle",
    "sad": "comfort_breath",
    "surprised": "attention_bloom",
    "angry": "soothing_amber",
    "fearful": "protective_glow",
    "disgusted": "settle_to_warm",
}


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
            emotion = message.get("emotion") or "neutral"
            await device.send_json(
                {
                    "type": "emotion",
                    "emotion": emotion,
                    "light_effect": EMOTION_TO_EFFECT.get(emotion, "calm_breath"),
                    "transcript": message.get("transcript", ""),
                    "hold_ms": 12000,
                }
            )
        elif message_type == "session.finished":
            return


@app.websocket("/v1/device/audio")
async def device_audio(websocket: WebSocket) -> None:
    expected_token = required_env("DEVICE_TOKEN")
    if bearer_token(websocket) != expected_token:
        await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
        return

    await websocket.accept()
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
            await websocket.send_json({"type": "ready", "sample_rate": 16000})

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
            await websocket.send_json(
                {"type": "gateway_error", "message": type(exc).__name__}
            )
        with suppress(Exception):
            await websocket.close(code=status.WS_1011_INTERNAL_ERROR)
