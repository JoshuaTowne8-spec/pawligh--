# Pawlight 宠物情绪灯

本项目现在只推荐使用 `firmware` 中的 **ESP-IDF 主程序**。Arduino 目录仅作为旧版本保留，不要与 ESP-IDF 固件混用。

## 第一次配置

1. 按照 `firmware/README.md` 完成接线。
2. 在 ESP-IDF PowerShell 中运行 `idf.py menuconfig`，只填写 Wi-Fi、电脑网关地址和设备令牌。
3. 将 `gateway/.env.example` 复制为 `gateway/.env`，填写阿里云 API Key，并确保设备令牌与 ESP32 中完全一致。

## 每次启动只做两件事

普通 PowerShell 启动网关：

```powershell
.\start_gateway.ps1
```

ESP-IDF PowerShell 编译、烧录并打开串口：

```powershell
.\flash_firmware.ps1 COM5
```

将 `COM5` 替换为设备实际串口。详细接线、首次配置和故障检查见 `firmware/README.md`。

## 当前交互

- 语音：阿里云返回温暖、快乐、平静、想念、难过之一，ESP32 播放对应固定灯效。
- 短按任意铜箔：播放 1.5 秒金橙追逐。
- 按住任意铜箔约 0.8 秒：播放 3 秒玫瑰金呼吸。
- 触摸灯效结束后：自动恢复最近一次语音情绪。

