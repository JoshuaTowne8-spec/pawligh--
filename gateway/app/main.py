import asyncio
import base64
import hmac
import json
import logging
import os
import uuid
from contextlib import suppress
from datetime import datetime
from pathlib import Path

import websockets
from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect, status


app = FastAPI(title="Pet Emotion Gateway", version="0.4.0")
logger = logging.getLogger("pet_gateway")
device_locks: dict[WebSocket, asyncio.Lock] = {}
connected_devices: set[WebSocket] = set()
mode_changed = asyncio.Event()

DATA_DIR = Path(
    os.getenv(
        "PET_GATEWAY_DATA_DIR",
        str(Path(__file__).resolve().parents[1] / "data"),
    )
)
RUNTIME_FILE = DATA_DIR / "runtime.json"
ARCHIVE_FILE = DATA_DIR / "archive.json"
EVENTS_FILE = DATA_DIR / "events.json"


def load_json(path: Path, fallback: object) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return fallback


def save_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    temporary.replace(path)


runtime_state = load_json(
    RUNTIME_FILE, {"always_on": False, "anniversary_keys": []}
)
if not isinstance(runtime_state, dict):
    runtime_state = {"always_on": False, "anniversary_keys": []}
runtime_state["always_on"] = bool(runtime_state.get("always_on", False))
runtime_state.setdefault("anniversary_keys", [])

archive_snapshot = load_json(ARCHIVE_FILE, {"events": []})
if not isinstance(archive_snapshot, dict):
    archive_snapshot = {"events": []}

pending_events = load_json(EVENTS_FILE, [])
if not isinstance(pending_events, list):
    pending_events = []

device_report = {"always_on": False}

EMOTION_FROM_TAG = {
    "温暖": "warm",
    "快乐": "happy",
    "平静": "calm",
    "想念": "miss",
    "难过": "sad",
}


def first_emotion(tags: object, default: str = "calm") -> str:
    if isinstance(tags, list):
        for tag in tags:
            if isinstance(tag, str) and tag in EMOTION_FROM_TAG:
                return EMOTION_FROM_TAG[tag]
    return default


def control_authorized(request: Request) -> None:
    """Protect LAN control; without a token only local processes are accepted."""
    expected = os.getenv("CONTROL_TOKEN", "").strip()
    if expected:
        supplied = request.headers.get("authorization", "").removeprefix("Bearer ")
        if not hmac.compare_digest(supplied, expected):
            raise HTTPException(status_code=401, detail="Invalid control token")
        return
    host = request.client.host if request.client else ""
    if host not in {"127.0.0.1", "::1", "localhost", "testclient"}:
        raise HTTPException(status_code=403, detail="Control API is local-only")


def public_state() -> dict[str, object]:
    return {
        "connected": bool(connected_devices),
        "always_on": bool(runtime_state["always_on"]),
        "audio_enabled": bool(connected_devices) and not runtime_state["always_on"],
    }


async def broadcast_device(payload: dict) -> int:
    sent = 0
    for device in list(connected_devices):
        try:
            await send_to_device(device, payload)
            sent += 1
        except Exception:
            logger.warning("Unable to send control message to an ESP32", exc_info=True)
    return sent


def remember_event(payload: dict) -> None:
    pending_events.append(payload)
    del pending_events[:-100]
    save_json(EVENTS_FILE, pending_events)


def matching_memory(transcript: str) -> dict | None:
    text = transcript.strip()
    if not text:
        return None
    events = archive_snapshot.get("events", [])
    if not isinstance(events, list):
        return None
    for item in events:
        if not isinstance(item, dict) or item.get("kind") != "memory":
            continue
        title = str(item.get("title", ""))
        body = str(item.get("text", ""))
        terms = [title]
        if "海" in title or "海" in body:
            terms.extend(("海边", "大海", "海浪", "浪花", "沙滩", "沙子", "看海"))
        if any(term and term in text for term in terms):
            return item
    return None

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


def cloud_close_detail(cloud: websockets.WebSocketClientProtocol) -> str:
    """Return close information that is useful when a cloud session ends."""
    code = getattr(cloud, "close_code", None)
    reason = getattr(cloud, "close_reason", "") or "(no reason supplied)"
    return f"code={code}, reason={reason}"


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
async def healthz() -> dict[str, object]:
    return {"status": "ok", **public_state()}


@app.get("/v1/control/state")
async def control_state(request: Request) -> dict[str, object]:
    control_authorized(request)
    return public_state()


@app.post("/v1/control/mode")
async def control_mode(request: Request) -> dict[str, object]:
    control_authorized(request)
    body = await request.json()
    if not isinstance(body, dict) or not isinstance(body.get("always_on"), bool):
        raise HTTPException(status_code=400, detail="always_on must be boolean")
    runtime_state["always_on"] = body["always_on"]
    save_json(RUNTIME_FILE, runtime_state)
    mode_changed.set()
    await broadcast_device({"type": "mode", "always_on": body["always_on"]})
    return public_state()


@app.post("/v1/control/effect")
async def control_effect(request: Request) -> dict[str, object]:
    control_authorized(request)
    body = await request.json()
    emotion = body.get("emotion") if isinstance(body, dict) else None
    if emotion not in {"warm", "happy", "calm", "miss", "sad"}:
        raise HTTPException(status_code=400, detail="Unknown emotion")
    hold_ms = min(max(int(body.get("hold_ms", 12000)), 1000), 300000)
    sent = await broadcast_device(
        {"type": "effect", "emotion": emotion, "hold_ms": hold_ms, "source": "web"}
    )
    return {"ok": sent > 0, "devices": sent}


@app.post("/v1/control/archive")
async def control_archive(request: Request) -> dict[str, bool]:
    control_authorized(request)
    body = await request.json()
    if not isinstance(body, dict) or not isinstance(body.get("events"), list):
        raise HTTPException(status_code=400, detail="Invalid archive")
    if len(body["events"]) > 2000:
        raise HTTPException(status_code=413, detail="Archive is too large")
    archive_snapshot.clear()
    archive_snapshot.update(body)
    save_json(ARCHIVE_FILE, archive_snapshot)
    return {"ok": True}


@app.get("/v1/control/events")
async def control_events(request: Request) -> dict[str, object]:
    control_authorized(request)
    return {"events": pending_events}


@app.post("/v1/control/events/{event_id}/ack")
async def acknowledge_event(event_id: str, request: Request) -> dict[str, bool]:
    control_authorized(request)
    pending_events[:] = [event for event in pending_events if event.get("id") != event_id]
    save_json(EVENTS_FILE, pending_events)
    return {"ok": True}


async def receive_device_messages(
    device: WebSocket,
    audio_queue: asyncio.Queue[bytes | str],
) -> None:
    while True:
        message = await device.receive()
        if message["type"] == "websocket.disconnect":
            logger.warning(
                "ESP32 WebSocket disconnect received: code=%s, reason=%s",
                message.get("code"),
                message.get("reason") or "(no reason supplied)",
            )
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
        elif command.get("type") == "status":
            device_report["always_on"] = bool(command.get("always_on", False))
        elif command.get("type") == "touch":
            touch_kind = "slow" if command.get("kind") == "slow" else "quick"
            now_monotonic = asyncio.get_running_loop().time()
            last_touch_at = float(device_report.get("last_touch_at", 0.0))
            if now_monotonic - last_touch_at >= 0.6:
                device_report["last_touch_at"] = now_monotonic
                remember_event(
                    {
                        "id": f"touch_{uuid.uuid4().hex}",
                        "type": "companion_triggered",
                        "trigger": "touch",
                        "touch_kind": touch_kind,
                        "emotion": "calm",
                        "transcript": "",
                        "created_at": datetime.now().astimezone().isoformat(),
                    }
                )


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
    try:
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
                memory = matching_memory(transcript)
                if memory:
                    emotion = first_emotion(memory.get("tags"), emotion)
                await send_to_device(
                    device,
                    {
                        "type": "effect" if memory else "emotion",
                        "emotion": emotion,
                        "raw_emotion": raw_emotion,
                        "transcript": transcript,
                        "source": "memory" if memory else "voice",
                        "hold_ms": 12000,
                    }
                )
                if memory:
                    remember_event(
                        {
                            "id": f"memory_{uuid.uuid4().hex}",
                            "type": "memory_recalled",
                            "memory_id": memory.get("id", ""),
                            "memory_title": memory.get("title", "一段回忆"),
                            "emotion": emotion,
                            "transcript": transcript,
                            "created_at": datetime.now().astimezone().isoformat(),
                        }
                    )
                elif transcript.strip():
                    remember_event(
                        {
                            "id": f"voice_{uuid.uuid4().hex}",
                            "type": "companion_triggered",
                            "trigger": "voice",
                            "emotion": emotion,
                            "transcript": transcript,
                            "created_at": datetime.now().astimezone().isoformat(),
                        }
                    )
            elif message_type == "session.finished":
                logger.info("Cloud session finished: %s", cloud_close_detail(cloud))
                return
    finally:
        logger.warning("Cloud result stream ended: %s", cloud_close_detail(cloud))


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
        while runtime_state["always_on"]:
            mode_changed.clear()
            if runtime_state["always_on"]:
                await mode_changed.wait()
        mode_changed.clear()
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
                mode_wait = asyncio.create_task(mode_changed.wait())
                try:
                    done, _ = await asyncio.wait(
                        {uplink, downlink, mode_wait}, return_when=asyncio.FIRST_COMPLETED
                    )
                    for task in done:
                        if task is not mode_wait:
                            task.result()

                    if mode_wait in done:
                        mode_changed.clear()
                        if runtime_state["always_on"]:
                            discard_queued_audio(audio_queue)
                            continue

                    # A normal return from the cloud receive loop means that the
                    # remote side closed the WebSocket. Treat it as a recoverable
                    # cloud failure and keep the ESP32 connection alive for retry.
                    if downlink in done:
                        raise RuntimeError(
                            "Cloud WebSocket closed: " + cloud_close_detail(cloud)
                        )
                finally:
                    # asyncio.wait does not cancel child tasks when this relay is
                    # cancelled. Always finish both tasks before reconnecting or
                    # closing the device socket, otherwise Python emits pending-task
                    # warnings and the next session can inherit stale audio work.
                    for task in (uplink, downlink, mode_wait):
                        if not task.done():
                            task.cancel()
                    await asyncio.gather(uplink, downlink, mode_wait, return_exceptions=True)
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


async def anniversary_scheduler() -> None:
    while True:
        try:
            now = datetime.now().astimezone()
            date_suffix = now.strftime("-%m-%d")
            events = archive_snapshot.get("events", [])
            triggered = runtime_state.get("anniversary_keys", [])
            if not isinstance(triggered, list):
                triggered = []
            if not runtime_state["always_on"] and isinstance(events, list):
                for item in events:
                    if not isinstance(item, dict) or item.get("kind") != "anniversary":
                        continue
                    if not str(item.get("date", "")).endswith(date_suffix):
                        continue
                    key = f"{now.date().isoformat()}:{item.get('id', '')}"
                    if key in triggered:
                        continue
                    emotion = first_emotion(item.get("tags"), "warm")
                    sent = await broadcast_device(
                        {
                            "type": "effect",
                            "emotion": emotion,
                            "hold_ms": 60000,
                            "source": "anniversary",
                        }
                    )
                    if sent:
                        triggered.append(key)
                        runtime_state["anniversary_keys"] = triggered[-400:]
                        save_json(RUNTIME_FILE, runtime_state)
                    break
        except Exception:
            logger.exception("Anniversary scheduler failed")
        await asyncio.sleep(30)


@app.on_event("startup")
async def start_scheduler() -> None:
    asyncio.create_task(anniversary_scheduler())


@app.websocket("/v1/device/audio")
async def device_audio(websocket: WebSocket) -> None:
    expected_token = required_env("DEVICE_TOKEN")
    if bearer_token(websocket) != expected_token:
        await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
        return

    await websocket.accept()
    logger.info("ESP32 connected: %s", websocket.client)
    device_locks.setdefault(websocket, asyncio.Lock())
    connected_devices.add(websocket)
    await send_to_device(
        websocket, {"type": "mode", "always_on": runtime_state["always_on"]}
    )
    # ESP32 uploads one 160 ms batch at a time. Twelve batches retain less than
    # two seconds of audio while still absorbing short cloud-side stalls.
    audio_queue: asyncio.Queue[bytes | str] = asyncio.Queue(maxsize=12)
    receiver = asyncio.create_task(receive_device_messages(websocket, audio_queue))
    relay = asyncio.create_task(relay_cloud_sessions(websocket, audio_queue))

    try:
        done, _ = await asyncio.wait(
            {receiver, relay}, return_when=asyncio.FIRST_COMPLETED
        )
        for task in done:
            task.result()
        logger.warning(
            "Device relay completed: receiver_done=%s, cloud_relay_done=%s",
            receiver in done,
            relay in done,
        )
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
        connected_devices.discard(websocket)
        logger.info("ESP32 disconnected: %s", websocket.client)
