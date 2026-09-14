# V2：低延迟传输实验

V2 借鉴商用副屏产品的公开架构方向：虚拟显示器、GPU 捕获、硬件 H.264、USB 本地 socket、Android 硬解和输入回传。它不包含对任何商用产品的逆向或其私有协议的复刻。

## 已采用

- Windows 端新增“低延迟 V2（实验）”档：12 Mbps、4 帧 VBV、120 帧 GOP、无 B 帧和 NVENC ultra-low-latency 编码。
- ADB localabstract 通道新增解码器就绪握手，避免 Windows 在 MediaCodec/Suface 尚未就绪时开始写帧。
- 桌面捕获也使用 FFmpeg 源时钟节流，防止 `ddagrab` 在重复桌面画面时突发写入 USB。
- Android 统计拆分为输入缓冲等待、提交至硬解输出、输出释放和待呈现帧数。

## Tab S4：1080p60 虚拟扩展屏，2026-09-14

稳定的 V2 实测日志为 `logs/usb-20260914-150039.jsonl`：共发送 1,315 帧，无编码队列过载；解码与呈现约 60 fps。发送端本地 socket 写入中位数为 0.26 ms。Android 端提交至硬解输出约 63–66 ms，接收至呈现回调中位数约 181 ms。设备不提供 Android 标准低延迟解码特性，主要剩余延迟在其硬解/Surface 队列。

TextureView 实验曾使接收至回调约 75 ms，但呈现仅约 45 fps，且待呈现帧数增长；它已归档，未纳入稳定 V2。
