# ESP32-S3 宠物情绪灯固件

本目录是可直接用 ESP-IDF 构建的完整固件。它完成以下数据链：

`INMP441 -> ESP32-S3 -> Wi-Fi/WebSocket -> 本机网关 -> 阿里云百炼 -> 情绪 -> WS2812`

MPR121 的触摸识别完全在设备本地运行，即使云端暂时断线，触摸灯效仍然有效。

音频仍以 16kHz、单声道、PCM16 采集。设备只把完整的 1280 字节/40ms 音频帧放入队列，每四帧合并成一个 5120 字节/160ms WebSocket 消息。缓存最多保留约 0.96 秒音频；拥塞时整帧丢弃，不会破坏 PCM16 字节对齐。Wi-Fi 省电模式已关闭，以提高持续上传稳定性。

## 接线

### INMP441（必须使用 3.3V）

| INMP441 | ESP32-S3 |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SCK/BCLK | GPIO17 |
| WS/LRCLK | GPIO18 |
| SD | GPIO16 |
| L/R | GND（选择左声道） |

### MPR121（必须使用 3.3V）

| MPR121 | ESP32-S3 |
|---|---|
| VIN/3V3 | 3V3 |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| E0 | 头部铜箔 |
| E1-E7 | 背部 7 片铜箔 |

本程序直接轮询 MPR121，所以 IRQ 不需要连接。铜箔到 MPR121 的线尽量短并远离 LED 电源线。3D 打印外壳过厚时，可在代码的 `pet_touch.c` 中提高灵敏度（降低触摸阈值 12 和释放阈值 6）。E8-E11 未启用，可留空。

### 两条 WS2812

默认使用两条独立信号线，30 灯灯带接 GPIO4，60 灯灯带接 GPIO5。

| 连接 | 30 灯灯带 | 60 灯灯带 |
|---|---|---|
| DIN/黄线 | GPIO4，经 330Ω 电阻 | GPIO5，经 330Ω 电阻 |
| +5V/红线 | 独立 5V 电源正极 | 同一个 5V 电源正极 |
| GND/黑线 | 5V 电源负极 | 5V 电源负极，并与 ESP32 GND 共地 |

建议在灯带输入端的 5V 与 GND 之间并联 1000µF/6.3V 或更高耐压的电解电容。90 颗灯按全白最坏情况约需 5.4A，建议使用 5V/6A 电源；本固件默认将亮度限制在 25%。不要让灯带大电流经过 ESP32 开发板的 5V 引脚。

没有 74AHCT125 时，短线台架测试可以先由 GPIO4 直接驱动 DIN。成品仍建议增加 74AHCT125/74HCT14 等 3.3V 到 5V 电平转换，以提高抗干扰能力。

如果两条灯带已经首尾串联，可在 `menuconfig` 打开 `PET_LED_CHAINED`，此时只接 GPIO4，GPIO5 留空。

## 构建与烧录

需要 ESP-IDF 5.3 或更高版本。请在 **ESP-IDF PowerShell** 中执行：

```powershell
cd C:\Users\thejo\Desktop\studio\Pet\firmware
idf.py set-target esp32s3
idf.py menuconfig
```

进入 `Pet emotion lamp`，填写：

- Wi-Fi SSID 与密码；
- Gateway URI：当前为 `ws://192.168.18.49:8080/v1/device/audio`；
- Device token：必须与 `gateway/.env` 里的 `DEVICE_TOKEN` 完全一致；
- 两条灯带的 LED 数量（当前默认 30 + 60）。
- Local voice detection RMS threshold：本地讲话检测阈值，默认 300。

令牌会进入本地 `sdkconfig`，该文件已被 Git 忽略。不要把令牌写进源码、截图或提交到仓库。

然后烧录。最简单的方式是在 ESP-IDF PowerShell 中运行根目录脚本：

```powershell
cd C:\Users\thejo\Desktop\studio\Pet
.\flash_firmware.ps1 COM5
```

把 `COM5` 换成设备管理器里实际看到的端口。首次烧录如果进不去下载模式，按住 BOOT，短按 RESET，再松开 BOOT。

## 使用前检查

1. PC 与 ESP32-S3 连接同一个 Wi-Fi。
2. 本机网关已启动并监听 `192.168.18.49:8080`。
3. Windows 防火墙允许这个端口在专用网络中入站。
4. 串口依次出现 `Got IP`、`Connected to emotion gateway`、`Gateway ready`。
5. 正常讲话结束约 0.5 秒后，网关将百炼结果归一化为五类产品情绪，基础灯效随之变化。

稳定运行时不应再出现 `dropped 1 bytes`。若网络长期阻塞，串口最多每秒汇总一次整帧丢弃数量；WebSocket 会在发送失败后自动重连并清除旧音频。

支持的产品情绪为 `warm`（温暖）、`happy`（快乐）、`calm`（平静）、`miss`（想念）、`sad`（难过）。五个语音灯效已经固化在设备中；短按任意铜箔触发 1.5 秒金橙追逐，持续触摸约 0.8 秒触发 3 秒玫瑰金呼吸。触摸灯效结束后自动恢复最近一次语音情绪。

设备上电后灯带保持关闭。第一次有效触摸会先播放对应的快摸或慢摸灯效，然后进入平静情绪；熄灯后再次触摸也会用相同方式唤醒。平静状态下，INMP441 在本地连续检测到约 120ms 的讲话时会重新开始静默计时；连续 10 秒没有讲话后，灯光用约 1 秒平滑熄灭。温暖、快乐、想念和难过情绪不会被这条平静超时规则提前关闭。AI 在灯光关闭期间返回的情绪不会自行点亮设备，必须由触摸唤醒。

如果触摸外壳也会不断延长平静灯效，可提高 `Local voice detection RMS threshold`；如果轻声讲话无法延长灯效，可适当降低。建议先以 300 测试，每次只调整 50～100。

## 主要源码

- `main/pet_audio.c`：INMP441 I2S 采集与 PCM16 转换
- `main/pet_cloud.c`：WebSocket 鉴权、音频上传和情绪接收
- `main/pet_touch.c`：MPR121 初始化与快/慢触摸识别
- `main/pet_leds.c`：五类语音情绪预设和两种触摸覆盖灯效
- `main/main.c`：系统启动顺序

## 网页控制和串口状态

网页开启长明灯后，设备持续播放平静灯效并暂停麦克风上传；关闭后回到正常待机和语音模式。网页和纪念日调度器还可以发送限时情绪灯效。触摸灯效优先显示，结束后恢复长明灯或当前基础状态。

串口只在对应交互真正触发时打印简短状态：`VOICE emotion=...` 表示语音情绪已经应用，`TOUCH quick` 或 `TOUCH slow` 表示快摸或慢摸已经触发。
