import asyncio
import base64
import json
import logging
import os
import uuid
from contextlib import suppress

import websockets
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, status


app = FastAPI(title="Pet Emotion Gateway", version="0.3.0")
logger = logging.getLogger("pet_gateway")
device_locks: dict[WebSocket, asyncio.Lock] = {}

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


async def receive_device_messages(
    device: WebSocket,
    audio_queue: asyncio.Queue[bytes | str],
) -> None:
    while True:
        message = await device.receive()
        if message["type"] == "websocket.disconnect":
            return
        audio = message.get("bytes")
        if audio:
            if audio_queue.full():
                with suppress(asyncio.QueueEmpty):
                    audio_queue.get_nowait()
            audio_queue.put_nowait(audio)
            continue

        text = message.get("text")
        if not text:
            continue
        command = json.loads(text)
        if command.get("type") == "finish":
            if audio_queue.full():
                with suppress(asyncio.QueueEmpty):
                    audio_queue.get_nowait()
            audio_queue.put_nowait("finish")


async def send_audio_to_cloud(
    audio_queue: asyncio.Queue[bytes | str],
    cloud: websockets.WebSocketClientProtocol,
) -> None:
    while True:
        item = await audio_queue.get()
        if item == "finish":
            await cloud.send(event("session.finish"))
            return
        await cloud.send(
            event(
                "input_audio_buffer.append",
                audio=base64.b64encode(item).decode("ascii"),
            )
        )


async def send_results_to_device(device: WebSocket, cloud: websockets.WebSocketClientProtocol) -> None:
    async for raw in cloud:
        message = json.loads(raw)
        message_type = message.get("type", "")

        if error := cloud_error(message):
            await send_to_device(
                device,
                {"type": "cloud_error", "message": str(error)}
            )
            raise error

        if message_type == "conversation.item.input_audio_transcription.completed":
            raw_emotion = message.get("emotion") or "neutral"
            transcript = message.get("transcript", "")
            emotion = normalize_emotion(raw_emotion, transcript)
            await send_to_device(
                device,
                {
                    "type": "emotion",
                    "emotion": emotion,
                    "raw_emotion": raw_emotion,
                    "transcript": transcript,
                    "hold_ms": 12000,
                }
            )
        elif message_type == "session.finished":
            return


def discard_queued_audio(audio_queue: asyncio.Queue[bytes | str]) -> int:
    discarded = 0
    while True:
        try:
            item = audio_queue.get_nowait()
        except asyncio.QueueEmpty:
            return discarded
        if isinstance(item, bytes):
            discarded += len(item)


async def relay_cloud_sessions(
    device: WebSocket,
    audio_queue: asyncio.Queue[bytes | str],
) -> None:
    api_key = required_env("DASHSCOPE_API_KEY")
    retry_seconds = 1

    while True:
        try:
            async with websockets.connect(
                dashscope_url(),
                extra_headers={
                    "Authorization": f"Bearer {api_key}",
                    "OpenAI-Beta": "realtime=v1",
                },
                open_timeout=15,
                ping_interval=20,
                ping_timeout=20,
                max_size=4 * 1024 * 1024,
            ) as cloud:
                await configure_cloud(cloud)
                discarded = discard_queued_audio(audio_queue)
                if discarded:
                    logger.info("Discarded %d bytes before cloud reconnect", discarded)

                await send_to_device(device, {"type": "ready", "sample_rate": 16000})
                retry_seconds = 1

                uplink = asyncio.create_task(send_audio_to_cloud(audio_queue, cloud))
                downlink = asyncio.create_task(send_results_to_device(device, cloud))
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
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            logger.exception("Cloud session failed; reconnecting in %s second(s)", retry_seconds)
            discard_queued_audio(audio_queue)
            with suppress(Exception):
                await send_to_device(
                    device,
                    {"type": "audio.pause", "message": str(exc)},
                )
            await asyncio.sleep(retry_seconds)
            retry_seconds = min(retry_seconds * 2, 10)


@app.websocket("/v1/device/audio")
async def device_audio(websocket: WebSocket) -> None:
    expected_token = required_env("DEVICE_TOKEN")
    if bearer_token(websocket) != expected_token:
        await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
        return

    await websocket.accept()
    device_locks.setdefault(websocket, asyncio.Lock())
    audio_queue: asyncio.Queue[bytes | str] = asyncio.Queue(maxsize=25)
    receiver = asyncio.create_task(receive_device_messages(websocket, audio_queue))
    relay = asyncio.create_task(relay_cloud_sessions(websocket, audio_queue))

    try:
        done, _ = await asyncio.wait(
            {receiver, relay}, return_when=asyncio.FIRST_COMPLETED
        )
        for task in done:
            task.result()
    except WebSocketDisconnect:
        return
    except Exception as exc:
        logger.exception("Device relay failed")
        with suppress(Exception):
            await send_to_device(
                websocket,
                {"type": "gateway_error", "message": str(exc)}
            )
    finally:
        receiver.cancel()
        relay.cancel()
        for task in (receiver, relay):
            with suppress(asyncio.CancelledError, Exception):
                await task
        device_locks.pop(websocket, None)
