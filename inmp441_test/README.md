# INMP441 独立声音测试

这个工程只读取 INMP441，并通过 USB 串口显示音量。它不启动 Wi-Fi、不连接电脑网关，也不会向阿里云发送任何数据。

## 接线图

```text
                 ESP32-S3                         INMP441
              ┌────────────┐                   ┌──────────┐
        3V3 ──┤ 3V3        ├──────────────────►│ VDD      │
        GND ──┤ GND        ├──────────────────►│ GND      │
              │            │                   │          │
     GPIO17 ──┤ GPIO17     ├──────────────────►│ SCK/BCLK │
     GPIO18 ──┤ GPIO18     ├──────────────────►│ WS/LRCLK │
     GPIO16 ◄─┤ GPIO16     │◄──────────────────│ SD       │
        GND ──┤ GND        ├──────────────────►│ L/R      │
              └────────────┘                   └──────────┘
```

| INMP441 | ESP32-S3 | 说明 |
|---|---|---|
| VDD | 3V3 | 只能使用 3.3V，不要接 5V |
| GND | GND | 必须共地 |
| SCK | GPIO17 | I2S 位时钟，也可能标为 BCLK |
| WS | GPIO18 | I2S 左右声道时钟，也可能标为 LRCLK |
| SD | GPIO16 | 麦克风数据输出 |
| L/R | GND | 选择左声道，与测试代码的 `I2S_STD_SLOT_LEFT` 对应 |

测试时只需要连接 ESP32-S3、INMP441 和 USB。灯带、MPR121、5V 灯带电源均可暂时断开。

## 烧录测试程序

在 ESP-IDF PowerShell 中执行：

```powershell
cd C:\Users\thejo\Desktop\studio\Pet\inmp441_test
idf.py set-target esp32s3
idf.py -p COM8 flash monitor
```

把 `COM8` 换成实际串口。退出串口监视器按 `Ctrl+]`。

## 正常输出

```text
INMP441 standalone test started
Pins: SD=GPIO16, SCK=GPIO17, WS=GPIO18, L/R=GND
No Wi-Fi or cloud connection is used. Speak near the microphone.

RMS=   45  PEAK=  180  DC=    12  [........................................]
RMS= 1250  PEAK= 6300  DC=    10  [########................................]
```

- 安静时 RMS 应较低，但通常不会始终等于 0。
- 说话、拍手时 RMS、PEAK 和 `#` 数量应明显上升。
- 一直显示 `NO SIGNAL`：优先检查 VDD、GND、SD 和 L/R。
- 数值不变或杂乱跳满：检查 SCK 与 WS 是否接反。
- 显示 `CLIPPING`：声音过大、供电噪声过强，或者接线不可靠。
- 如果 L/R 接到了 3V3，代码必须改用右声道；本测试推荐直接将 L/R 接 GND。

