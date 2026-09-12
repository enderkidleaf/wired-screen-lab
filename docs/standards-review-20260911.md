# 屏幕传输代码规范审查（2026-09-11）

本轮仅审查代码、历史提交、已有日志及官方文档；不修改运行代码、不安装、不启动手机测试。范围为 Android 接收端、Windows 两条编码输出路径及诊断脚本，不是整个 IDD 驱动的完整合规认证。

## 结论

原来的接收代码存在初始化和退出管理缺口，新原生编码路径又引入了事件计数和输出缓冲兼容性缺口。发现缺口不等于证明它导致当前 25 fps。暂不能把根因归为手机硬件或码流兼容性，更不能依据现有对照排除传输负载因素。

历史基线：4825150 已有不提交 CSD 和限时 join 后释放解码器的代码；c5628b7 加入诊断；8cec9a4 接通原生 USB。当前 UI 默认仍为 auto，CLI 原生模式需 --native。之前“应该恢复默认”的说法容易误导：原生模式并未被设为默认。

## 确定的代码缺口

### 1. 旧接收端没有明确提交 H.264 初始化数据（高优先级）

位置：native/android/src/com/wiredscreen/usb/MainActivity.java:129、173。

configure 没有 csd-0/csd-1；所有视频包以 flags=0 入队，没有独立 BUFFER_FLAG_CODEC_CONFIG。实现依赖解码器识别混在首个视频包内的 SPS/PPS。

Android 文档的初始化契约是通过 format 的 CSD，或在帧数据之前提交 codec-config 缓冲。自适应播放中途在关键帧携带配置另有规则，不能当作所有初始化行为的保证。此缺口旧版已存在；旧版能播放不代表跨设备兼容性已经正确。需要优先明确初始化状态、配置归属和首个关键帧顺序，尚未证明这是本次降帧根因。[MediaCodec 官方文档](https://developer.android.com/reference/android/media/MediaCodec#Initialization)

### 2. 原生编码器丢失输入事件额度（中优先级，条件触发）

位置：native/pc/GpuVideoEncoder.h:149。

收到 METransformNeedInput 时只有 credits<3 才计数，否则已消费事件被丢弃。三帧在途限制应控制提交，不应销毁尚未使用的事件额度；若 MFT 提前提供多于三份额度且不补发，可能停滞。现有 pending.size()<3 已能独立限制在途数。目前没有记录证明本机触发过此条件。[Microsoft 异步 MFT 契约](https://learn.microsoft.com/en-us/windows/win32/medfound/asynchronous-mfts)

### 3. 原生输出缓冲忽略对齐要求（中优先级，条件触发）

位置：native/pc/GpuVideoEncoder.h:158。

在调用方提供输出 sample 的分支中，仅按 cbSize 调用 MFCreateMemoryBuffer，没有处理 cbAlignment。对于要求额外对齐的 MFT，不能保证满足契约；若 MFT 自行分配输出，则不走该分支。不能将此直接解释为当前手机降帧。[MFT_OUTPUT_STREAM_INFO](https://learn.microsoft.com/en-us/windows/win32/api/mftransform/ns-mftransform-mft_output_stream_info)

### 4. 旧接收端释放时未确认输出线程终止（中优先级）

位置：native/android/src/com/wiredscreen/usb/MainActivity.java:190。

drain.join(1500) 超时后仍直接 stop/release，不检查线程是否还在调用 codec；若输出调用卡住，退出会与使用重叠。configure/start 阶段的异常还位于接收循环 finally 之外，不能保证释放已创建的 decoder/HandlerThread。这属于静态可见的生命周期缺口，主要影响失败与重连，未证明解释稳定阶段低帧率。

## 测试与结论中的确定问题

### 5. 对照负载不等价，之前因果判断过强

读取已有 artifacts/receiver-replay*.json，在 3–8 秒窗口：

| 回放 | 平均解码 fps | 接收视频 Mbps | 平均输入等待 ms |
|---|---:|---:|---:|
| 原生 | 25.40 | 7.3227 | 38.96 |
| 软件参考 | 59.81 | 0.1647 | 0.86 |
| 原生去 SEI | 25.44 | 7.3346 | 38.89 |
| 原生去重复 PPS | 33.15 | 9.7477 | 32.64 |

参考编码命令没有匹配码率/包大小/编码复杂度。表中 Mbps 是实际接收量，受背压影响，不是链路容量或编码器目标码率。它证明轻负载参考流可以达到 60 fps，不能单独证明 USB 在等负载下无瓶颈，也不能证明重复 PPS 是根因。

### 6. 回放实验还改变了未控制的条件

位置：native/scripts/test_receiver_replay.py。

- 每次重新生成原生样本，未保存同一固定输入供所有变体使用。
- --aud 无条件追加 AUD；既有原生样本已经包含 AUD，实验变成重复分隔符，不能验证“缺少 AUD”。
- 短样本循环时没有验证 IDR、安全重启边界和参数状态；两种编码器循环长度不同。
- 软件参考并非之前已验证的完整 FFmpeg 扩展桌面实现，不能代替旧版基线复测。
- 仅以平均解码 fps 判定通过；没有验证帧对应、显示、持续性。它只适合局部吞吐排查。

以上实验数据保留为线索，不作为选择正式码流变换的依据。

### 7. 呈现回调与延迟的定义必须保持准确

位置：MainActivity.java:132；Engine.cs:159–165。

Android 明确说明呈现回调可能延迟、合并，旧 Android 还可能缺失。每秒回调次数不是严格的面板刷新统计。现有 receiveToCallbackMs 和发送到确认包含回调处理/回传等待，不能作为亮屏延迟；心跳与视频共享接收循环，“USB 往返”还包含解码阻塞。[OnFrameRenderedListener](https://developer.android.com/reference/android/media/MediaCodec.OnFrameRenderedListener)

## 未发现足够依据判错的部分

- 当前长度前缀读取不需要等待下一帧分隔符；旧 AnnexBFramer 仍存在，但不是目前 AVI/原生发送主线。
- 对有参考依赖的编码帧按顺序传输，队列过载停会话，比任意丢 P 帧继续解码更合理。停会话不是最终恢复方案。
- MF 输入用 100 ns，Android PTS 用微秒，按 60 fps 构造时单位没有明显算错。时间戳未保留实际源采样间隔是设计限制，不应称作单位错误。
- 既有码流检查得到 time_scale=120、num_units_in_tick=1，是 60 fps 时序，不是 120 fps 配置错误。
- 重复 PPS、本身存在的 SEI 或 Baseline profile 不能仅凭重复/存在就认定非法或低帧率根因。
- 原生目标平均码率不等于明确设置了 CBR/VBV；当前缺少实际协商参数记录，是可观测性和可比性不足，不直接等于 API 非法。

## 后续修改顺序（本轮不执行）

1. 固定历史基线、APK/EXE 版本及同一测试负载，修正实验口径。
2. 优先补齐接收端初始化与生命周期契约，验证旧路径不回退。
3. 独立修正原生事件额度和缓冲分配契约，避免混入性能参数变化。
4. 先检查固定原生样本的完整访问单元、参数集和帧对应，再在相同负载下逐项对照。
5. 原生路径满足正确性、持续 60 fps 与实际延迟门槛后，再考虑替换常用路径。

审查到此为止；本报告列出的是可落实的代码问题与证据边界，不宣称已找到或修复帧率回退根因。

## 2026-09-12 修复记录

本轮完成第 2、3 项：输入事件额度单独计数，不再受三帧在途上限截断；调用方分配输出缓冲时使用对齐分配，将 MFT 的字节对齐要求转换为 MFCreateAlignedMemoryBuffer 的掩码参数。未调整编码参数和手机端。

新增 --contract-test，覆盖八次提前输入授权完整消费、无授权消费拒绝、实际缓冲地址满足 0/1/16/64/256 字节对齐。已纳入 test_native_encoder.py。/W4 /WX 编译、契约检查、三帧颜色回归及持续二进制流独立解码通过。没有手机性能实测，不能宣称改善 25 fps 问题；第 1、4 项及对照测试口径仍待处理。

### 2026-09-12 修复后实机短测

小米 15S Pro（25042PN24C），原生模式、动态扩展桌面，目标测试 20 秒。手机端与编码参数未调整。实际发送 294 帧后因编码帧队列过载提前停止；启动后每秒解码采样约 27.2–35.9 fps，后段继续积压。未通过持续 60 fps 验收，不能证明本轮契约修复解决了降帧，也不能用单轮数值变化宣称性能改善。日志：native/dist/logs/usb-20260912-085406.jsonl；控制台：artifacts/native-usb-contract-fix-20260912.txt。

### 2026-09-12 Android 显式初始化修正

新增 AvcInitialization，将会话首包的 SPS/PPS 转为带四字节起始码的 codec-config 缓冲，在 IDR 画面之前提交；首帧剩余 NAL 原样保留。当前协议要求首包同时包含 SPS/PPS/IDR，不缓存不完整初始化；异常首包明确终止。之后的视频包不做去重或 SEI/AUD 变换。

离线 JVM 检查覆盖混合起始码、初始化顺序、画面字节保留、转义序列、不完整数据及非 IDR 首帧拒绝。完整构建通过，APK 已安装到小米 15S Pro。

- 原生短测：248 帧后过载停止，启动后约 27–36 fps。日志 usb-20260912-085938.jsonl。显式初始化已执行，但未解决低帧率。
- 兼容短测：--virtual --compat --dynamic --seconds 20，实际 h264_nvenc，发送 1183 帧，正常停止；去掉前五次统计后平均解码约 60 fps，无队列过载。日志 usb-20260912-090029.jsonl。确认旧兼容路径在本次短测正常；不等于所有旧编码器或长期稳定性验收。

退出生命周期修正和等负载性能对照仍未完成。本轮仅改变初始化，不调整缓冲容量、编码参数或解码线程等待策略。
