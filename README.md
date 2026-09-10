# 有线副屏实验室

## 当前进度

2026-09-10：已在本机与小米 15S Pro 跑通真正的 Windows 虚拟扩展桌面 → H.264 → ADB USB → 安卓解码显示，用户已确认手机看到了扩展桌面。

驱动创建一块 1920×1080、60 Hz 虚拟显示器。30 秒短测发送 1750 帧，发送端丢弃过期帧 10 帧；去除前 5 次预热采样后的 24 次统计，平均解码 59.98 fps、呈现回调 59.94 fps，USB 平均往返 6.93 ms、最大 47.56 ms。采样有波动，尚未完成持续动态画面验收；呈现回调不是独立画面测量，USB 往返不是端到端显示延迟。三星 Tab S4 尚待复测。

本机证据：native/dist/logs/usb-20260910-190118.jsonl、artifacts/usb-virtual-test.txt。驱动使用本机测试签名，尚不是正式分发版本。

管理员启动 PC 程序，注册虚拟副屏后选择桌面并开始 USB 投屏；程序会自动定位虚拟显示器。命令行短测支持 WiredScreen.exe --virtual --seconds 30，结束后移除虚拟屏。当前机器虚拟屏映射到 adapter 0/output 1，其他显卡布局尚待验证。

目标：Windows 通过 USB 向三星 Tab S4 提供 **1920×1080、60 fps 扩展桌面**。
现阶段可用小米 15S Pro 验证传输，最终性能必须在 Tab S4 复测。

下面的 Node.js + 浏览器 WebRTC 内容是**历史网络原型**，仅保留用于回溯；当前开发主线是后文的原生 ADB USB 版。

## 启动

电脑已安装 Node.js 22 或以上时，双击 `start.cmd`，或在本目录运行 `npm start`。
在电脑 Edge / Chrome 打开 <http://localhost:3210>。不需要安装 npm 依赖。

1. 手机用支持数据传输的 USB 线连接电脑，在手机设置里开启 **USB 网络共享**。
2. 关闭手机 Wi-Fi，避免媒体实际走无线；必要时关闭移动数据以排除互联网路线。
3. 电脑点击「发送 1080p 动态测试画面」，先确认链路；或点「选择共享画面」选择屏幕。
4. 电脑会列出本机网卡对应的短地址和 8 位配对码。在手机浏览器打开 **USB 网卡对应地址**，输入配对码，点击「连接电脑」。不要选择 VMware、WSL、Meta 或 WLAN 网卡。
5. 手机点「全屏」。先观察分辨率、接收解码 fps，再进行持续测试。
6. 完成后电脑点「停止」，关闭启动窗口可退出服务。

若 Windows 首次弹出防火墙提示，需要允许此测试使用的 Node.js 和浏览器在对应网络通信。应用不会自动修改防火墙。WebRTC 媒体使用动态端口，仅开放 HTTP 3210 端口不一定够。

无法打开手机页面时，先检查 USB 网络共享、地址和防火墙。能配对但没画面时，检查浏览器 WebRTC、VPN/代理虚拟网卡和防火墙。USB 网络共享不支持的设备需要后续 AOA／其他传输适配。

**ADB reverse 只转发 HTTP 不会自动转发 WebRTC 的 UDP 视频，不应把它当作此版本的 USB 替代方案。**

## 测量 1080p60

- 设置是目标，接收端的实际统计才是测量。
- 动态测试画面本身受浏览器动画调度、CPU/GPU 和后台节流影响。不要最小化发送页。真实屏幕捕获另行测试。
- 静止画面可能主动降低发送帧率，因此不能用静止桌面判断是否支持 60 fps。
- 默认优先 H.264，可与 VP8 对比；优先级不能保证硬件编码。
- 预热约 10 秒后开始观察，持续至少 5 分钟。导出接收页记录，在电脑运行：

  `node scripts/analyze.cjs "接收端记录.json"`

- 当前工具门槛：连续 300 秒、全部采样 1920×1080、至少 95% 采样达到 57 fps、无超过 2.5 秒的采样间断。最好记录 6 分钟覆盖预热。
- 这只是“接近 60 帧”的工程验收门槛。解码帧率不是屏幕实际呈现帧率；网络 RTT 也不是从电脑到平板屏幕的总延迟。
- 路由需核对 USB 网卡，浏览器可能隐藏地址。手机关闭 Wi-Fi，拔 USB 后视频应中断，是实际验证的一部分。

## 开发与测试

- `npm test`：配对、鉴权、会话撤销、输入校验、性能记录验收规则。
- `node test/browser-smoke.cjs`：需要可加载的 Playwright 和本机 Edge；启动本机发送/接收页面、验证视频分辨率和停止行为、保存截图与采样到 `artifacts/`。
- 测试时可设 `BENCHMARK_SECONDS=30`；完整持续测试设为 360。浏览器测试需要正常本机网络，受限沙箱可能令 ICE 无法连接。

配对会话和信令仅存内存，10 分钟无活动过期。没有云服务、STUN 或 TURN。配对链接具有访问权限，不应公开。该 HTTP 原型仅用于可信的本地/USB 网络。

## 下一阶段

见 [ROADMAP.md](ROADMAP.md) 和 [docs/architecture.md](docs/architecture.md)。

## 原生 USB 版（当前主线）

构建原生 PC/安卓端所需工具会放在项目的 `.tools/` 中。先运行：

`python native/scripts/bootstrap.py`

再运行：

`python native/scripts/build.py`

产物在 `native/dist/`：`WiredScreen.exe`、`WiredScreen.apk` 和 ADB/FFmpeg 运行文件。PC 程序支持图形界面，也支持：

`WiredScreen.exe --self-test`

`WiredScreen.exe --install`

`WiredScreen.exe --test --seconds 30`

原生版本通过 ADB USB 的 localabstract socket 搬运 H.264 Annex-B 帧；安卓端使用 MediaCodec，并优先启用低延迟解码。PC 端可捕获已有桌面画面；IDD 虚拟显示器驱动已可构建至 `native/dist/idd/`，签名、安装和实际捕获验收仍待完成。

`native/scripts/sign_install_test_idd.ps1` 是用于本机开发验证的下一步：它可开启 Windows 测试签名模式（需要重启）、创建本机自签名证书、将其加入本机信任存储、为驱动包签名并安装。它不能作为面向其他用户的正式驱动分发方案。

小米 MIUI 可能阻止 ADB 安装。需要在手机“开发者选项”中允许 USB 调试和 USB 安装，并解锁手机确认安装；若仍提示 `INSTALL_FAILED_USER_RESTRICTED`，可手动把 `native/dist/WiredScreen.apk` 传到手机安装。安装动作由手机系统确认，不会绕过安全限制。

2026-09-10 实机更新：已确认 Windows 测试签名模式生效，驱动已通过本机测试证书签名并加入驱动库（oem54.inf）。修正了 PC 端系统接口 DLL 引用和签名证书存储选择。注册设备仍返回 HRESULT 0x8007007E，尚未枚举出虚拟显示器；副屏和 1080p60 验收仍未完成。原生构建及协议自检通过。
