# Pet Emotion Gateway

This gateway keeps the Alibaba Cloud Model Studio API key off the ESP32. The
device sends 16 kHz, mono, signed 16-bit little-endian PCM frames to the gateway
over WebSocket. The gateway relays them to `qwen3-asr-flash-realtime` and returns
the transcript and one of five fixed emotion IDs.

## Device protocol

- URL: `wss://YOUR_DOMAIN/v1/device/audio`
- Header: `Authorization: Bearer DEVICE_TOKEN`
- Payload: binary PCM; current ESP32 firmware sends 160 ms (5120 bytes) per message
- Control: send `{"type":"finish"}` before closing a session

Example result:

```json
{
  "type": "emotion",
  "emotion": "sad",
  "transcript": "我今天真的很想你",
  "hold_ms": 12000
}
```

## 最简单的本机运行方式

第一次运行，在项目根目录执行：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r gateway\requirements.txt
Copy-Item gateway\.env.example gateway\.env
```

在 `gateway\.env` 中填写阿里云 API Key 和设备令牌。以后每次只需双击或运行：

```powershell
.\start_gateway.ps1
```

浏览器打开 `http://127.0.0.1:8080/healthz`，看到 `{"status":"ok"}` 就表示网关正常。

启动脚本会监听电脑的所有网络接口。确保 Windows 防火墙只允许专用网络访问
8080 端口，并把 ESP32 的 Gateway URI 设置为：

```text
ws://YOUR_PC_LAN_IP:8080/v1/device/audio
```

其中 `YOUR_PC_LAN_IP` 替换为电脑的 WLAN IPv4 地址，设备令牌必须与
`gateway\.env` 内的 `DEVICE_TOKEN` 完全相同。

The API host for the current default workspace has already been filled into
`.env.example`. Only `DASHSCOPE_API_KEY` and `DEVICE_TOKEN` remain secret and
must be entered locally; never send them in chat or commit `.env`.

In production, put Caddy, Nginx, or an Alibaba Cloud load balancer with a valid
TLS certificate in front of port 8080. Only expose port 443 publicly.

## PawLight web control

The local PawLight web app proxies device requests to this gateway. The control
API only accepts loopback clients unless `CONTROL_TOKEN` is configured. The web
app synchronizes the pet archive so the gateway can persist calm always-on
mode, pause cloud audio while that mode is active, play anniversary emotions,
and create a companion-page event when seaside memories are mentioned.

Runtime state is stored in the ignored `gateway/data` directory. API keys and
device tokens remain only in the ignored `gateway/.env` file.
