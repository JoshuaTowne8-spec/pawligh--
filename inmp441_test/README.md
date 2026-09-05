# INMP441 + MPR121 独立测试

这个工程读取 INMP441 声音和 MPR121 的 8 个触摸电极，并通过 USB 串口显示结果。它不启动 Wi-Fi、不连接电脑网关，也不会向阿里云发送任何数据。

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

### MPR121 接线

```text
ESP32-S3                    MPR121
┌──────────┐               ┌──────────┐
│ 3V3      ├──────────────►│ VIN/VCC  │
│ GND      ├──────────────►│ GND      │
│ GPIO8    │◄─────────────►│ SDA      │
│ GPIO9    ├──────────────►│ SCL      │
└──────────┘               │ IRQ  NC  │  不连接
                           │ E0 ─ 铜箔 │  头部
                           │ E1 ─ 铜箔 │  背部第1片
                           │ ...      │
                           │ E7 ─ 铜箔 │  背部第7片
                           └──────────┘
```

| MPR121 | ESP32-S3 / 铜箔 | 说明 |
|---|---|---|
| VIN/VCC | 3V3 | 使用 3.3V |
| GND | GND | 与 ESP32-S3、INMP441 共地 |
| SDA | GPIO8 | I2C 数据 |
| SCL | GPIO9 | I2C 时钟 |
| IRQ | 不连接 | 测试程序采用轮询，不需要 IRQ |
| E0 | 头部铜箔 | 头部触摸点 |
| E1-E7 | 7 片背部铜箔 | 背部触摸点 |

测试时需要连接 ESP32-S3、INMP441、MPR121 和 USB。WS2812 灯带及其 5V 电源仍可断开。

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
INMP441 + MPR121 standalone test started
Pins: SD=GPIO16, SCK=GPIO17, WS=GPIO18, L/R=GND
Touch: SDA=GPIO8, SCL=GPIO9; E0=HEAD, E1-E7=BACK
Short touch=QUICK, hold for 0.8s=SLOW
No Wi-Fi or cloud connection is used. Speak near the microphone.

RMS=   45  PEAK=  180  DC=    12  [........................................]
RMS= 1250  PEAK= 6300  DC=    10  [########................................]
TOUCH START  mask=0x01  electrode=E0(HEAD)
QUICK TOUCH  duration=236ms  electrode=E0(HEAD)
TOUCH START  mask=0x08  electrode=E3(BACK)
SLOW TOUCH   duration>=800ms  electrode=E3(BACK)
```

- 安静时 RMS 应较低，但通常不会始终等于 0。
- 说话、拍手时 RMS、PEAK 和 `#` 数量应明显上升。
- 一直显示 `NO SIGNAL`：优先检查 VDD、GND、SD 和 L/R。
- 数值不变或杂乱跳满：检查 SCK 与 WS 是否接反。
- 显示 `CLIPPING`：声音过大、供电噪声过强，或者接线不可靠。
- 如果 L/R 接到了 3V3，代码必须改用右声道；本测试推荐直接将 L/R 接 GND。
- 短触并松开会输出 `QUICK TOUCH`；持续按住约 0.8 秒会输出 `SLOW TOUCH`。
- 如果启动时提示 `MPR121 not responding at 0x5A`，检查 3.3V、共地、SDA 和 SCL，尤其不要把 SDA/SCL 接反。
