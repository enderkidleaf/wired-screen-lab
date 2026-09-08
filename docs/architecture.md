# 架构与取舍

> 进度声明：当前实现是有线屏幕读取与投屏原型，不是 Windows 虚拟扩展屏。只有完成后文的 IDD 驱动阶段，Windows 才会识别出新的副显示器。

## 已实现的数据链路

Windows 浏览器 getDisplayMedia / 1920×1080 canvas.captureStream(60)
→ RTCPeerConnection 编码（优先 H.264，可切 VP8）
→ 本地 UDP / DTLS / SRTP
→ Android 浏览器 WebRTC 解码
→ video 元素。

Node.js HTTP 服务只负责静态页面、配对与 SDP/ICE 交换。画面不经过 Node 服务。
USB 网络共享提供 IP 网络，浏览器仍自行选择 ICE 通道，因此必须验证实际媒体路由。
接收端通过数据通道向电脑回传统计。

## 边界

浏览器不能创建 Windows 虚拟屏。第一阶段刻意将视频链路与显示驱动分开：先证明设备能够接收视频，再开发 IDD。原型可共享已存在的第二屏，但不承担创建它的职责。

第二阶段显示驱动路线：

1. 安装与当前工具链兼容的 WDK，确认 IddCx/UMDF 编译环境。
2. 以微软 IndirectDisplay 示例为参考，实现单显示器与 1920×1080@60 显示模式。
3. 完成驱动包、签名、安装卸载，在隔离测试环境验证系统识别。
4. 首先用现有浏览器捕获新增屏幕验证端到端功能。
5. 若性能不足，再将 IDD 交付的 D3D 图像接入原生硬件编码器；平板接收层可以保留，传输需接入原生 WebRTC。

不会把未经编译的驱动骨架标成“可运行”，也不会为了试驱动自动关闭系统签名保护。

## 诊断口径

源 fps：浏览器媒体源统计；发送 fps：相邻采样 framesEncoded 差值 / 时间；接收 fps：framesDecoded 差值 / 时间。
processingMs 是每帧平均编解码时间，不包括显示队列、传输和扫描显示。
指标记录保留最近约 30 分钟；分析器拒绝发送端记录、短测试、错误分辨率和长采样间断。

## 官方参考

- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview
- https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay
- https://developer.chrome.com/docs/web-platform/screen-sharing-controls
- https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/getStats
- https://developer.android.com/develop/connectivity/usb/accessory
