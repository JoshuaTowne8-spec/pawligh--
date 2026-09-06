# PawLight Memories（本机联动版）

本地运行已经配置为单用户模式。D1 保存宠物档案，R2 保存照片和视频；硬件命令通过同一台电脑上的 PawLight 网关转发，不会把阿里云 API Key 发送到浏览器或 ESP32。

从上级目录运行：

```powershell
.\start_web.ps1
```

打开 `http://localhost:3000`。网关必须同时通过上级目录的 `start_gateway.ps1` 运行。

`.env.local` 是本机配置，已被 Git 忽略；`.env.example` 是不含密钥的配置模板。原有 `.openai/hosting.json`、D1 和 R2 配置保持不变，因此项目仍可继续使用原来的 Sites/Cloudflare 部署方式。
