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

## PawLight 网页联动

网页项目位于 `pawlight-memories`，已配置本机单用户模式、本地 D1/R2、真实设备状态和网关代理。

运行时打开两个 PowerShell：

```powershell
.\start_gateway.ps1
```

```powershell
.\start_web.ps1
```

然后访问 `http://localhost:3000`。网页会把档案同步给网关，并支持：

- 开启/关闭平静长明灯；开启期间 ESP32 停止上传麦克风音频；
- 每年在重要日期触发该记录首个情绪标签的灯效；
- 倾诉内容提到海边、大海、浪花、沙滩等海边记忆时，播放“快乐”灯效并打开新的陪伴记录；
- 将时间线中的记忆真实发送为 ESP32 灯效。

本机网页数据位于项目的 `.wrangler` 目录，网关状态和待处理事件位于 `gateway/data`。这些本地数据和密钥均已排除在 Git 提交之外。
