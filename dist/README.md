# DianBiao v1.0.10 · 16 MB 固件

本版心跳不再携带 `meters`，12 路电量使用 `meter_energy` 单独上报。Topic `{MAC}` 为带冒号 MAC，裸 `MAC` 为无冒号 MAC；默认 Topic 升级后包含冒号，上位机订阅/发布需匹配，需要原无冒号格式时使用 `dianbiao/meter/MAC`。APP 设备管理能力保留。见 [v1.0.10 验证记录](../docs/验证记录-v1.0.10.md)。

| 文件 | 用途 |
|---|---|
| `DianBiao_v1.0.10_16MB_ota.bin` | 已运行本项目且分区一致的 N16R8 设备，OTA 升级 |
| `DianBiao_v1.0.10_16MB_factory.bin` | N16R8 首次串口烧录，起始地址 `0x0` |

已有设备使用 OTA 文件更新，完成后刷新网页。页面随应用固件更新，无需上传文件系统。首次烧录镜像用于初始化，已有设备保留配置和计量数据时使用 OTA 文件。

固件版本、大小、MD5 和 SHA256 见 `manifest.json`。尚未实板烧录验证。
