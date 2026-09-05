# PetEmotionLamp Arduino IDE 版本

本工程用于 ESP32-S3 N16R8，控制两条独立 WS2812B 灯带（30 颗和 60 颗）、INMP441 麦克风及 MPR121 九点触摸板，并通过现有电脑网关接入阿里云百炼。

## Arduino IDE 环境

1. 安装 Arduino IDE 2.x。
2. 在开发板管理器安装 `esp32 by Espressif Systems` 3.x。
3. 在库管理器安装：
   - `FastLED`
   - `ArduinoJson`（7.x）
   - `Adafruit MPR121`
   - `Adafruit BusIO`
   - `WebSockets by Markus Sattler`
4. 打开 `PetEmotionLamp.ino`。

## 需要修改的配置

先将同目录的 `config.example.h` 复制为 `config.h`，再打开 `config.h`，只修改：

```cpp
#define PET_WIFI_SSID       "你的Wi-Fi名称"
#define PET_WIFI_PASSWORD   "你的Wi-Fi密码"
#define PET_DEVICE_TOKEN    "gateway/.env里的DEVICE_TOKEN"
```

阿里云 API Key 不得写入 ESP32，继续保存在 `gateway/.env`。

控制页面位于：

```text
http://192.168.18.49:8080/control
```

网关当前配置为：

```cpp
#define PET_GATEWAY_HOST "192.168.18.49"
#define PET_GATEWAY_PORT 8080
```

如果电脑局域网 IP 改变，需要同步修改 `PET_GATEWAY_HOST`。

## Arduino IDE 开发板选项

建议选择：

- Board：`ESP32S3 Dev Module`
- CPU Frequency：`240MHz (WiFi)`
- Flash Size：`16MB (128Mb)`
- PSRAM：`OPI PSRAM`
- USB CDC On Boot：`Enabled`
- Upload Speed：`460800`（稳定后可改 921600）

选择实际 COM 端口后单击“上传”。

## 引脚和供电

| 设备 | ESP32-S3 |
|---|---|
| 灯带 A DIN（30 颗） | GPIO4，串联 330Ω |
| 灯带 B DIN（60 颗） | GPIO5，串联 330Ω |
| MPR121 SDA / SCL / IRQ | GPIO8 / GPIO9 / GPIO10 |
| INMP441 SD / SCK / WS | GPIO16 / GPIO17 / GPIO18 |

两条灯带由独立 5V 电源供电。90 颗灯建议使用至少 5V/6A 电源；灯带 GND、ESP32 GND 和电源负极必须共地。两条灯带的 5V/GND 应分别从电源并联引出，不要让 60 颗灯的电流经过 30 颗灯带。60 颗灯带最好在首尾两端都注入 5V/GND。每条灯带输入端建议并联 1000µF 电容。程序亮度默认限制为 64/255，并额外设置了 3A 软件功率上限。

INMP441 和 MPR121 只能接 3.3V。

## 正常串口输出

波特率设为 115200，启动后应依次看到：

```text
[led] two independent strips ready: 30 + 60 LEDs
[touch] MPR121 ready: electrodes E0-E8
[audio] INMP441 ready: PCM16 mono, 16 kHz
[wifi] IP: ...
[cloud] WebSocket connected
[cloud] gateway ready; audio upload enabled
```

MPR121 只使用 8 个电极：E0 为头部，E1-E7 为背部 7 片铜箔，E8-E11 留空。主人说话结束约 0.5 秒后，网关将百炼结果归一化为温暖、快乐、平静、想念、难过五类情绪之一并切换基础灯效。控制页面可以修改七个预设的颜色、动画类型、亮度、速度、周期和镜像方式；短按或快速划过多个触摸区触发金色追逐，持续触摸约 0.8 秒触发玫瑰金呼吸。
