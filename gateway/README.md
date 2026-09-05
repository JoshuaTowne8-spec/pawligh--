# Pet Emotion Gateway

This gateway keeps the Alibaba Cloud Model Studio API key off the ESP32. The
device sends 16 kHz, mono, signed 16-bit little-endian PCM frames to the gateway
over WebSocket. The gateway relays them to `qwen3-asr-flash-realtime` and returns
only the transcript, emotion, and light-effect ID.

## Device protocol

- URL: `wss://YOUR_DOMAIN/v1/device/audio`
- Header: `Authorization: Bearer DEVICE_TOKEN`
- Payload: binary PCM, preferably 40 ms (1280 bytes) per frame
- Control: send `{"type":"finish"}` before closing a session

Example result:

```json
{
  "type": "emotion",
  "emotion": "sad",
  "light_effect": "comfort_breath",
  "transcript": "我今天真的很想你",
  "hold_ms": 12000
}
```

## Run locally

1. Copy `.env.example` to `.env` and fill it locally.
2. With Docker, run `docker compose up --build`.
3. Without Docker, create a virtual environment, install `requirements.txt`,
   and start Uvicorn with `--env-file gateway/.env`.
4. Check `/healthz` on the selected bind address.

For a same-Wi-Fi prototype, bind Uvicorn to the PC's specific WLAN address,
not `0.0.0.0`. Configure the ESP32 URL as:

```text
ws://YOUR_PC_LAN_IP:8080/v1/device/audio
```

Use `Authorization: Bearer DEVICE_TOKEN` during the WebSocket handshake.

The API host for the current default workspace has already been filled into
`.env.example`. Only `DASHSCOPE_API_KEY` and `DEVICE_TOKEN` remain secret and
must be entered locally; never send them in chat or commit `.env`.

In production, put Caddy, Nginx, or an Alibaba Cloud load balancer with a valid
TLS certificate in front of port 8080. Only expose port 443 publicly.
