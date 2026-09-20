# GKD Mini — RC3.6

面向 **原版 GKD Mini（Ingenic X1830 / GKD350）** 的 Linux 6.1.28 系统项目。当前使用 SimpleMenu 作为游戏前端。**不支持 GKD Mini Plus。**

这是 RC3.6 的公开源码快照，包含系统服务、共享 UI、内核补丁、构建脚本和测试。首次公开仓采用独立历史；版本号对应已验收的 RC3.6 功能基线。公开树进行了构建路径规范化，不能据此声称与已安装固件逐字节一致。

**当前不提供固件下载或空白卡镜像。** 完整固件构建依赖未随仓库分发的原机用户态输入、工具链及启动图片，详见[构建说明](docs/BUILDING.md)。

## 已实现的功能

- A 日常环境与独立 R 恢复环境，签名系统更新及恢复流程。
- 游戏启动/退出与进程生命周期管理；原始、Xbox、PlayStation 输入方案及按应用配置的原生菜单路由。
- 共用设置、电源、USB 菜单、确认文本页、滚动条、OSD 和旋转 loading。
- 截图、音量/亮度设置、自动休眠、低电量提示和保护。
- 游戏卡插拔提示、USB STORAGE/DEBUG 切换和等待状态。
- 既有模拟器兼容修复脚本；模拟器包不随仓库提供。

系统管理的游戏卡目录统一为 `gkd-update`、`screenshots`、`roms`、`saves`、`states`。更新入口位于系统设置，包路径为 `gkd-update/system.gkdupdate`。

## 文档

[配置](docs/CONFIGURATION.md) · [架构](docs/ARCHITECTURE.md) · [构建与测试](docs/BUILDING.md) · [验收与待办](docs/STATUS.md) · [变更记录](CHANGELOG.md) · [贡献](CONTRIBUTING.md) · [安全](SECURITY.md) · [许可证与来源](NOTICE.md)

## English

Linux 6.1.28 system software for the **original GKD Mini (X1830 / GKD350)**, with SimpleMenu as the current frontend. GKD Mini Plus is not supported.

RC3.6 provides application lifecycle management, configurable input routing, shared menus/OSD/loading, screenshots, USB mode management, power handling, and signed A/R system updates. This repository publishes source, patches and tests. It does **not** distribute a firmware image, ROMs, BIOS files, emulator packages, recovery images or signing keys.

The public snapshot normalizes build-host paths and omits an unreviewed boot image. Full firmware reproduction still requires external inputs; see [BUILDING](docs/BUILDING.md). Existing hardware acceptance applies to the development RC3.6 firmware, not a separately built public image. See [STATUS](docs/STATUS.md) for verified behavior and remaining work.

## License

Project-owned material is available under **GPL-2.0-only**, except where a file declares different terms. Existing GPL-2.0-or-later and Linux syscall-note declarations remain intact. Third-party font exceptions and attribution are retained; see [NOTICE](NOTICE.md) and [LICENSE](LICENSE). No license is granted here for omitted third-party binaries or artwork.
