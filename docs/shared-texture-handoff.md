# 共享纹理交接：第一步实现

2026-09-11：共享纹理池已接入可选的 IDD 实验构建，新增受控句柄交接协议。独立进程交接与真实 IDD 三帧取帧验证通过；手机编码会话尚未使用它。

## 驱动连接进度

`GpuHandoff.h` 在独立线程处理本地命名管道，只有提升权限的管理员客户端通过连接令牌检查后才能得到资源句柄信息。管道拒绝远程客户端，使用首实例限制和受限 DACL。客户端仅提供 SecurityIdentification 识别级别，服务端读取识别令牌核对提升的管理员组后立即恢复自身上下文，不能借用客户端权限访问资源。客户端核对实际管道服务进程；驱动探针还核对系统目录的 WUDFHost.exe 及 LocalService/System 账户。普通用户产品化访问需要另建授权代理，此实验接口不是按应用签名授权。

客户端从已核对的进程复制四个句柄，元数据映射为只读；成功打开 GPU 纹理后发出确认，驱动才开始复制画面。每次握手读写最多等待三秒，等待可取消。连接关闭后不再发布。每个交换链只接受一次连接；连接失败、退出或设备失效后必须重新注册虚拟屏，自动重连待实现。

构建 `native/scripts/build_idd.ps1 -EnableGpuHandoff` 会生成单独的 `native/dist/idd-gpu` 包。普通构建不启用接口。配置脚本在上游样例的取帧处插入受编译开关保护的发布调用；纹理池在取帧循环外创建，画面线程不执行管道 I/O。已有帧归还顺序保留。

`native/scripts/build_gpu_test.cmd` 同时生成 `GpuHandoffProbe.exe`。普通权限运行验证非管理员拒绝和错误服务 PID 拒绝；管理员运行会启动一个不继承句柄的独立进程，验证授权、句柄复制、1080p 逐像素读取及断连处理。这两类检查均已通过。实验驱动 WDK 构建通过（零警告、零错误）；安装后可用管理员脚本 `native/scripts/test_idd_handoff.ps1` 验证实际 IDD 的三帧画面。探针的调试权限仅在其进程内临时启用，用于打开经过身份检查的 UMDF 进程，不改变系统权限设置。

实验驱动已使用已有证书签名并安装（本机 oem59.inf）。真实 UMDF 环境下的管道、识别令牌核验、句柄复制和三帧 GPU 取帧通过：源帧号 71、72、73，分辨率均为 1920×1080。测试结束已移除虚拟屏。本地证据：artifacts/idd-handoff-test.txt。手机传输仍走原来的桌面复制/FFmpeg 链路，尚未接入本组件。安装使用 `sign_install_test_idd.ps1 -UseExistingTrustedCertificate -GpuHandoff`，沿用已有可信证书；不需要重新导入信任或改动启动设置。

## 已实现

`native/pc/SharedTexturePool.h` 提供单生产者、单消费者、同显卡的三槽共享纹理池，固定 BGRA 格式。生产端复制到自有 GPU 纹理，不持有源纹理供消费者后续访问。共享信息包含版本、尺寸、格式、显卡 LUID、QPC 频率、帧序号与捕获时间戳。该时间戳由调用者提供：独立池测试使用测试时间戳，实际 IDD 路径在取得系统表面后记录 QPC。它表示驱动观察到帧的时刻，不是桌面开始绘制或手机亮屏时刻。

生产端仅以零超时申请可写纹理槽。三槽全部占用时返回 S_FALSE，调用者可以跳过尚未编码的原始帧。消费端选出本轮扫描中序号最大的可读帧，释放其余旧原始帧；借用纹理在 Release 前有效。编码器必须先完成读取或复制到自己的输入表面，再释放槽位。此设计仍有一次 GPU 复制，不宣称零拷贝。

纹理通过未命名 NT 句柄交接，避免创建公开的全局纹理名称。早期池测试仅继承明确列出的四个句柄（三张纹理与元数据映射）；新驱动连接使用上面的进程身份检查及受控复制，不使用继承。

AcquireSync 只有 S_OK 才代表取得所有权；WAIT_TIMEOUT 表示跳过，WAIT_ABANDONED 转为会话失效。设备丢失或模式变化需要销毁整池并重新协商。消费者退出时不让生产端无限等待；这不代表已经实现自动重连。

## 验证

运行 `native/scripts/build_gpu_test.cmd`，再运行 `native/dist/SharedTextureTest.exe`。编译启用 /W4 /WX。

- 两个独立进程、独立 D3D11 设备共享 1920×1080 纹理。
- 三帧使用不同像素值，消费端得到最新帧号，并逐像素验证画面。
- 满池拒绝新增原始帧；消费后旧帧不会重复取出。
- 消费进程正常退出后，全部槽位可重用；再次填满仍有界。
- 消费进程持有纹理时被强制结束，生产端识别失效并返回，不等待消费者。

上述检查在本机通过。逐像素检查的 CPU 读回只存在于验证程序，生产交接接口不下载像素。未测量吞吐、GPU 调度耗时、手机显示延迟或长期稳定性。

## 下一步接入约束

1. 增加普通用户的授权代理和多会话隔离；当前管理员实验模式已完成实际取帧验证，不开放给所有用户。
2. 覆盖驱动断连、模式变化和设备丢失，完成每次重新注册的新资源池协商。
3. 原生同显卡 GPU 转换、编码及源帧时间戳已完成本地接入，详见 [原生编码进度](native-gpu-encoder.md)；仍需把编码包接入 USB 手机会话。
4. 完成设备丢失、断连、模式变化和重新协商后，先启用实验模式，保留当前桌面复制回退；不默认替换已验证链路。

同步规则参考 [Microsoft AcquireSync 文档](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-acquiresync)，驱动帧归还顺序参考 [FinishedProcessingFrame 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxswapchainfinishedprocessingframe)。

管道访问位与实例创建权限参考 [Microsoft 命名管道安全文档](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)。
