# 有线副屏实验室

通过 **ADB USB** 将 Windows 的 1080p 桌面画面显示在 Android 平板上，不使用 Wi-Fi、USB 网络共享或浏览器。当前版本已经具备 Windows 虚拟扩展桌面、Android 接收端、低延迟 EGL 最新帧渲染和断线后自动等待重连。

## 先看这里：推荐使用方式

1. 用支持数据传输的 USB 线连接 Windows 电脑和 Android 平板。
2. 在平板的开发者选项中开启“USB 调试”，解锁设备后允许这台电脑调试。
3. 安装并打开 Android App，保持它在前台。可在 Windows 程序点“安装手机 App”，也可手动安装 `WiredScreen.apk`。
4. 启动 Windows 程序 `WiredScreen.exe`。
5. 为获得最佳延迟体验，选择 **“低延迟 V2（实验）”** 和 **`libx264`** 编码器。
6. 先用“动态测试画面”确认手机端解码和呈现统计接近 60 fps。
7. 要作为扩展屏使用，点击“启动 USB 副屏”，按 Windows 提示授予管理员权限。程序会创建虚拟显示器并连接平板；随后可在 Windows 显示设置中排列这个新屏幕。

断开 USB 后，Android App 会继续在前台等待下一次连接，无需重新打开 App。

## 已完成的方案

- Windows 通过 ADB `localabstract` USB socket 发送 H.264 视频，不走网络。
- Windows IDD 虚拟显示器可以注册为真正的扩展桌面。
- Android 使用 MediaCodec 硬件解码，并通过 EGL 外部纹理渲染；渲染器以最新帧优先，避免旧帧在显示端继续排队。
- V2 启动流程会完成最小解码初始化后丢弃连接期积压，并从新的关键帧开始正式传输。
- PC 和手机端都提供帧率、USB 往返和呈现回调统计；桌面捕获权限不足会在建立传输前给出明确错误。

### 已测结果

三星 Tab S4 的 1080p 动态画面短测保持约 60 fps。Android 0.6.0 的 EGL 最新帧渲染路径中，正式流确认呈现延迟平均 **55.3 ms**、最高 **85.7 ms**。这是接收端呈现回调测量，不等于屏幕光子级延迟；实际鼠标手感仍会受电脑性能、USB 线材和桌面内容影响。

## 使用前准备

### Android 平板

- Android 8 或更高版本。
- 开启 USB 调试并授权电脑。
- 保持接收端 App 在前台；可在 App 内隐藏控制面板、展开统计或启用保持亮屏。

### Windows 电脑

- Windows 10/11、支持数据传输的 USB 线和已安装的 ADB 驱动。
- 运行 `WiredScreen.exe` 所在目录中的所有运行文件：`ffmpeg.exe`、`adb.exe`、`AdbWinApi.dll`、`AdbWinUsbApi.dll`。
- 使用“启动 USB 副屏”需要管理员授权。
- 虚拟显示器驱动当前为 **本机测试签名** 版本，只适合开发验证，不是可直接分发的正式驱动。

测试模式安装、退出方式、风险边界及正式签名路线见 [IDD 驱动签名与测试模式](docs/driver-signing-and-test-mode.md)。

## 常见问题

| 现象 | 处理方式 |
| --- | --- |
| 没有已授权的 USB 设备 | 重新插拔、解锁平板，在“允许 USB 调试吗？”中确认。 |
| 手机显示等待连接 | 保持 App 前台，在电脑端重新开始；不必重开手机 App。 |
| 无法访问 Windows 桌面捕获 | 从当前已登录的 Windows 桌面启动程序。需要扩展屏时使用“启动 USB 副屏”。 |
| 帧率明显低于 60 fps | 选择 `libx264` 和“低延迟 V2（实验）”，关闭高负载程序，再确认 USB 线支持数据传输。 |
| 无法创建虚拟显示器 | 检查测试驱动是否已安装，并重新以管理员权限启动“启动 USB 副屏”。 |

## 从源码构建

运行原生 PC/Android 构建前，先准备工具链：

```powershell
python native/scripts/bootstrap.py
python native/scripts/build.py
```

构建产物位于 `native/dist/`。可用以下命令检查协议和 USB 传输：

```powershell
native/dist/WiredScreen.exe --self-test
native/dist/WiredScreen.exe --install
native/dist/WiredScreen.exe --test --seconds 30
```

`native/scripts/sign_install_test_idd.ps1` 用于本机测试签名 IDD 驱动；它可能要求开启 Windows 测试签名模式并重启。请勿把该测试签名驱动当作正式分发方案。

## 目录说明

- `native/android/`：Android 接收端、MediaCodec 和 EGL 最新帧渲染。
- `native/pc/`：Windows UI、ADB USB 传输、编码和虚拟显示器控制。
- `native/idd/`：Windows 间接显示驱动源码。
- `native/scripts/`：构建、测试和驱动安装脚本。
- `docs/`：协议、低延迟设计和实测记录。

## 历史尝试（仅供回溯）

仓库中仍保留 Node.js、浏览器和 WebRTC 的网络投屏原型，以及早期的编码、驱动和传输实验记录。它们不属于当前使用路径，也不需要 USB 网络共享、浏览器配对、Wi-Fi 或 Node.js。当前主线以本 README 说明的 **原生 ADB USB 副屏** 为准。
