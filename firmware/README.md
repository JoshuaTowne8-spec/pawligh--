# ESP32-S3 宠物情绪灯固件

本目录是可直接用 ESP-IDF 构建的完整固件。它完成以下数据链：

`INMP441 -> ESP32-S3 -> Wi-Fi/WebSocket -> 本机网关 -> 阿里云百炼 -> 情绪 -> WS2812`

MPR121 的触摸识别完全在设备本地运行，即使云端暂时断线，触摸灯效仍然有效。

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
| IRQ | GPIO10 |
| E0 | 头部铜箔 |
| E1-E7 | 背部 7 片铜箔 |

铜箔到 MPR121 的线尽量短并远离 LED 电源线。3D 打印外壳过厚时，可在代码的 `pet_touch.c` 中提高灵敏度（降低触摸阈值 12 和释放阈值 6）。E8-E11 未启用，可留空。

### 两条 WS2812

默认按照片里的串联方式：第一条 DOUT 已接到第二条 DIN，所以只需一个数据输入。

| 灯带输入 | 连接 |
|---|---|
| DIN/黄线 | GPIO4，串联 330Ω 电阻 |
| +5V/红线 | 独立稳定 5V 电源正极 |
| GND/黑线 | 5V 电源负极，并与 ESP32 GND 共地 |

建议在灯带输入端的 5V 与 GND 之间并联 1000µF/6.3V 或更高耐压的电解电容。20 颗灯按最坏情况应预留约 1.2A；本固件默认将亮度限制在 25%。不要让灯带大电流经过 ESP32 开发板的 5V 引脚。

没有 74AHCT125 时，短线台架测试可以先由 GPIO4 直接驱动 DIN。成品仍建议增加 74AHCT125/74HCT14 等 3.3V 到 5V 电平转换，以提高抗干扰能力。

如果两条灯带并非串联、而是各有一个 DIN，可在 `menuconfig` 关闭 `PET_LED_CHAINED`，第二条数据线接 GPIO5。

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
- 两排各自的 LED 数量（当前默认 10 + 10）。

令牌会进入本地 `sdkconfig`，该文件已被 Git 忽略。不要把令牌写进源码、截图或提交到仓库。

然后烧录：

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

把 `COM5` 换成设备管理器里实际看到的端口。首次烧录如果进不去下载模式，按住 BOOT，短按 RESET，再松开 BOOT。

## 使用前检查

1. PC 与 ESP32-S3 连接同一个 Wi-Fi。
2. 本机网关已启动并监听 `192.168.18.49:8080`。
3. Windows 防火墙允许这个端口在专用网络中入站。
4. 串口依次出现 `Got IP`、`Connected to emotion gateway`、`Gateway ready`。
5. 正常讲话结束约 0.5 秒后，网关将百炼结果归一化为五类产品情绪，基础灯效随之变化。

支持的产品情绪为 `warm`（温暖）、`happy`（快乐）、`calm`（平静）、`miss`（想念）、`sad`（难过）。五个语音灯效已经固化在设备中；快速连续滑过至少三个触摸区会触发 1.5 秒金色追逐，持续触摸约 0.8 秒会触发 3 秒玫瑰金呼吸。

## 主要源码

- `main/pet_audio.c`：INMP441 I2S 采集与 PCM16 转换
- `main/pet_cloud.c`：WebSocket 鉴权、音频上传和情绪接收
- `main/pet_touch.c`：MPR121 初始化与快/慢触摸识别
- `main/pet_leds.c`：五类语音情绪预设和两种触摸覆盖灯效
- `main/main.c`：系统启动顺序
