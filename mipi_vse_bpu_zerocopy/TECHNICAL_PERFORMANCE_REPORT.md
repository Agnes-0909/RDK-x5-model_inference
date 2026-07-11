# MIPI-VSE-BPU 双路零拷贝技术与性能报告

## 1. 报告摘要

本报告记录 `mipi_vse_bpu_zerocopy` 在 RDK X5 上的一次双路 100 帧工程基线。左右 SC132GS 图像分别经 MIPI host 0/2、VIN、ISP 和 VSE；VSE 将 `1088×1280 NV12` 输入的中心 `1088×1088` 区域缩放为 `640×640 NV12`。应用用 `hbSysRegisterMem` 注册 VSE 输出物理内存，交给两个独立模型实例和 worker（每路独立处理线程）；二者均绑定同一个 `bpu_core0`（BPU 硬件执行核心 0），随后在 CPU 上执行 YOLO11 后处理（postprocess，即输出扫描、解码、坐标映射和 NMS）并归还 VSE 帧。

实测事实如下：左右各成功处理 100 帧，失败、超时、释放错误和物理地址不一致均为 0；200/200 次推理满足 `vse_phy == dnn_phy`，每路注册缓存为 3 miss/97 hit。左右 worker 端到端均值分别为 63.424 ms 和 63.116 ms，其中 BPU 均值分别为 59.965 ms 和 59.599 ms，后处理均值分别为 3.319 ms 和 3.376 ms。

FPS 指每秒完成帧数；aggregate FPS 是左右路合计完成速率，pair 是按完成顺序将第 k 个左/右结果配成一对，paired FPS 是每秒完成的 pair 数。窗口计数口径为 31.708932 aggregate FPS、15.854466 paired FPS；更严格按 `N-1` 个完成间隔计算的**首末完成区间速率**为 31.550388 aggregate FPS、15.778467 paired FPS。后者排除了首个完成前的启动时间，但没有剔除预热、检查滚动稳定性或覆盖多次运行，不能称为或证明稳态性能。结合 BPU 内部计时，`wait_ms` 占总耗时的绝大部分，单 core BPU 调度/执行是当前主要瓶颈。双 worker 没有使 BPU 算力翻倍，其价值是隔离两路生命周期与背压，并允许一侧 CPU 后处理同另一侧任务调度/执行发生重叠。

## 2. 测试对象与环境

| 项目 | 已知信息 | 证据/说明 |
|---|---|---|
| 硬件平台 | RDK X5；工程定位为 GS130W 双目 | 工程 README |
| 图像传感器 | 左右均为 SC132GS，芯片 ID `0x0132` | 100 帧日志 `vio.sensor_selected` |
| 路由 | 左：MIPI host 0、I2C bus 6、地址 `0x32`；右：MIPI host 2、I2C bus 4、地址 `0x33` | 启动事件及传感器选择事件 |
| BPU | 两模型实例都使用 `bpu_core0`；core index 0、mask 1 | 200 条 `dnn.inference_complete` 及源码配置 |
| 模型文件 | `/root/mipi_proj/model/model.bin` | `app.startup` |
| 模型接口 | `yolo11m_bayese_640x640_nv12`，单路输入为 640×640 NV12、614400 bytes，6 个输出 | 模型加载代码中的强校验 |
| 测试参数 | 每路 100 帧，score 0.25，NMS 0.45，`LOG_LEVEL=info` | 运行脚本默认值及启动事件 |
| 主性能测试 | 2026-07-11 22:18:31 至 22:18:41，UTC+08:00（Asia/Shanghai） | 日志 `wall_time`；目录 `run_20260711_221831` |
| debug 测试 | 2026-07-11，UTC+08:00，每路 2 帧，`LOG_LEVEL=debug` | 目录 `run_20260711_221544` |
| 构建环境 | README 记录 Docker 容器 `3c543521b4ca`，交叉构建产物为 `build_aarch64/mipi_vse_bpu_zerocopy` | 工程 README |
| 频率/温度/功耗模式 | 未记录 | 不据此推断降频或功耗表现 |
| SDK/Runtime 精确版本 | 未记录 | 本报告不杜撰版本号 |

主性能数据源为板端 `root@192.168.1.12:/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221831/` 下的 `events.jsonl`、`summary.json` 和 `run.log`；后处理分段数据源为 `root@192.168.1.12:/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221544/events.jsonl`。报告数值来自已校验的 `/tmp/mipi_vse_performance_metrics.json`，未重新运行板端测试。

## 3. 系统链路与并发架构

```text
左 SC132GS -> MIPI0 -> VIN -> ISP -> VSE 640x640 NV12 -> [容量 1] -> 左 worker -> register/cache -> 左模型 --+
                                                                                                            |
                                                                                                     共享 bpu_core0
                                                                                                            |
右 SC132GS -> MIPI2 -> VIN -> ISP -> VSE 640x640 NV12 -> [容量 1] -> 右 worker -> register/cache -> 右模型 --+

BPU 完成 -> 左 worker -> 左 CPU 后处理 -> 左 VseFrame release
BPU 完成 -> 右 worker -> 右 CPU 后处理 -> 右 VseFrame release
```

每路 VSE 配置 3 个连续缓冲区。主线程先后取得左右帧，再通过 move-only 的 `VseFrame` 将所有权移入对应的容量 1 队列。队列只有一个 `optional<VseFrame>` 槽位：worker 未取走上一帧时，生产者在 `push` 阻塞，从而把处理能力不足反向传播到取帧侧，限制在途帧和延迟积累；关闭队列会唤醒生产者和消费者。100 帧运行中累计 push 等待只有左 0.068 ms、右 0.036 ms，说明该次运行未出现显著的生产者阻塞，但容量 1 的背压约束仍然成立。

`VseFrame` 禁止复制，只允许 move。它持有 `hbn_vnode_image_t` 的释放闭包；所有权从主线程移到队列、再移到 worker。worker 完成推理和后处理后显式调用 `release()`，异常或提前退出时析构函数也提供一次性释放保障。

正常与异常退出均遵守同一资源顺序：先 `close()` 两队列，再 `join()` 两 worker，确认没有 worker 继续访问 VSE 缓冲；随后 `shutdownProcessors()` 清空两路注册缓存并调用 `hbSysUnregisterMem`；最后才 `pipelines.stop()` 停止 VIO。即“先 join、再 clear cache、最后停 VIO”，避免工作线程或注册描述符引用已经关闭的 VSE 资源。

## 4. 零拷贝实现与证据

零拷贝路径先验证 VSE 描述符必须是 640×640、Y/UV stride 均为 640、两 plane 大小分别为 409600/204800 bytes，且 UV 物理地址紧随 Y plane；不满足条件即报错退出，不提供复制兜底。通过后，应用以 Y plane 地址和 614400 bytes 调用 `hbSysRegisterMem`，再把注册所得 `phyAddr/virAddr` 直接填入 DNN 输入 tensor。

| 证据 | 左 | 右 | 合计/结论 |
|---|---:|---:|---|
| 推理地址相等 | 100/100 | 100/100 | **200/200 `vse_phy == dnn_phy`** |
| 地址不一致 | 0 | 0 | 0 |
| 注册缓存 miss | 3 | 3 | 与每路 3 个 VSE buffer 一致 |
| 注册缓存 hit | 97 | 97 | 每路命中率 97% |
| core index/mask 不符 | 0 | 0 | 200 次均为 index 0、mask 1 |

此外，CMake 测试 `no_image_copy` 静态扫描推理路径，禁止 `hbSysWriteMem`、图像 `memcpy` 和 `cv::resize`；布局校验也禁止静默退化到 CPU copy/resize。这些是应用可见路径的静态策略门禁。

证据边界必须保留：上述地址一致性、注册缓存和源码门禁证明的是本应用把 VSE 物理缓冲直接绑定为 DNN 输入，且应用推理路径没有显式图像复制；它们**不证明也不宣称 SDK、驱动或硬件内部绝无复制**。

## 5. 耗时指标定义

所有耗时使用 `std::chrono::steady_clock`，单位为 ms。

| 指标 | 起止边界 | 包含/不包含 |
|---|---|---|
| DNN `submit_ms` | `hbDNNInfer` 调用前后 | 任务提交调用 |
| DNN `wait_ms` | `hbDNNWaitTaskDone` 调用前后 | BPU 排队和执行等待，当前日志无法继续拆分二者 |
| DNN `invalidate_ms` | 逐个输出调用 `hbSysFlushMem(...CACHE_INVALIDATE)` 前后 | 6 个输出 tensor 的 CPU cache invalidate |
| DNN `release_ms` | `hbDNNReleaseTask` 调用前后 | DNN task 释放；不是 VSE frame release |
| DNN `total_ms` / worker `bpu_ms` | `hbDNNInfer` 前至 `hbDNNReleaseTask` 后 | **submit + wait + invalidate + release** 及调用间的极小开销 |
| worker `postprocess_ms` | `Yolo11Postprocessor::run` 内部 | 分类扫描、DFL 解码、坐标映射和 NMS |
| worker `end_to_end_ms` | worker 出队后、调用 `processor_->process` 前，至该调用返回后 | 注册缓存查询/首次注册、BPU、CPU 后处理，以及 processor 内日志事件构造和 `AsyncLogger::log` 同步入队；不含 VSE frame release |
| queue push/pop wait | 相应条件变量等待前后，summary 为逐路累计值 | 容量 1 背压或消费者等帧的累计等待 |

`worker.end_to_end_ms` 不是曝光到检测输出的全链路延迟：它不含 MIPI 曝光、VIN、ISP/VSE 硬件处理、主线程 `hbn_vnode_getframe`、左右取帧配对和入队前/队列等待，也不含处理后的 `hbn_vnode_releaseframe`。BPU 局部 timer 和 postprocess 局部 timer 均在相应日志事件之前结束，因而排除日志事件开销。与之不同，`end_to_end_ms` 包围整个 `processor_->process` 调用，包含 processor 内 registration、DNN 和 postprocess 事件的字段/对象构造，以及 `AsyncLogger::log` 的同步入队开销（包括 mutex、时间戳处理和队列满时的等待）；它不包含异步 writer 线程中的文本/JSON 序列化和文件 I/O，也不包含 timer 结束后才提交的 `worker.frame_processed` 事件。

## 6. 测试方法与数据口径

主测试从 `events.jsonl` 按 `channel` 收集 100 条 `dnn.inference_complete` 和 100 条 `worker.frame_processed`。分位数在升序数组位置 `(n-1)×p` 上做线性插值，分别取 P50/P90/P95/P99；这一定义用于避免不同统计工具默认算法不一致。计算均值同 `summary.json` 逐路交叉检查，绝对差均小于 0.001 ms。正文耗时统一显示到 0.001 ms，完整精度保留在已校验 JSON 中。

每路只有 100 个样本时，线性插值 P99 的位置为 98.01，几乎完全由最慢 1–2 个样本决定。因此本报告的 P99 仅为探索性统计，不用于性能认证或优化验收。约 100 帧的基线应主要观察 P90、P95、max 及相应原始慢样本；若要把 P95/P99 作为验收指标，应每路每次至少采集 1000 帧、重复多次运行，并报告跨运行离散度或置信区间。

吞吐口径如下：

| 口径 | 公式 | 时间窗 | 结果 |
|---|---|---:|---:|
| window aggregate | 200 个完成帧 / 首个至末个 `worker.frame_processed` | 6.307371 s | 31.708932 FPS |
| window paired | 100 个名义 pair / 同一首末 worker 完成窗 | 6.307371 s | 15.854466 FPS |
| intercompletion interval rate（aggregate） | 199 个 aggregate 完成间隔 / 首末 worker 完成窗 | 6.307371 s | **31.550388 FPS** |
| intercompletion interval rate（pair） | 左右各自按完成时间排序，第 k 个左右结果取较晚时刻作为 pair 完成；99 个 pair 间隔 / 首末 pair 完成窗 | 6.274374 s | **15.778467 FPS** |

window 指标保留为原验收窗口口径，但首末两个完成事件之间实际只有 `N-1` 个间隔，使用 `N/span` 会高估该区间内的完成间隔速率。因此瓶颈讨论引用 31.550388 aggregate FPS 与 15.778467 paired FPS 作为**本次首末完成区间速率**。该口径排除了首个完成前的启动时间，却没有预热剔除、滚动窗口稳定性检查或多次运行，不能证明稳态。已校验 JSON 沿用历史键名 `steady_intercompletion_fps`/`steady_paired_fps`，其正确语义以本段为准。以上均为完成端速率，不是传感器曝光帧率或单帧完整链路延迟。

## 7. 100 帧性能结果

下表所有耗时单位为 ms，正文按 3 位小数显示，数值由已校验 JSON 四舍五入而来。DNN `total_ms` 与 worker `bpu_ms` 是同一批推理、同一计时边界的直接映射，不是两组独立样本；为保留两个日志字段的可追溯性，分别列在 DNN 分阶段表和 worker 表中，分析时不得重复计数。

DNN 分阶段：

| 路 | 指标 | count | min | mean | P50 | P90 | P95 | P99 | max |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 左 | `total_ms` | 100 | 46.664 | 59.965 | 60.135 | 61.785 | 66.377 | 68.708 | 71.102 |
| 左 | `submit_ms` | 100 | 0.044 | 0.058 | 0.054 | 0.063 | 0.079 | 0.141 | 0.269 |
| 左 | `wait_ms` | 100 | 46.247 | 59.558 | 59.728 | 61.419 | 66.018 | 68.351 | 70.657 |
| 左 | `invalidate_ms` | 100 | 0.285 | 0.326 | 0.308 | 0.339 | 0.406 | 0.818 | 1.130 |
| 左 | `release_ms` | 100 | 0.015 | 0.021 | 0.020 | 0.026 | 0.029 | 0.033 | 0.038 |
| 右 | `total_ms` | 100 | 38.002 | 59.599 | 60.233 | 61.671 | 63.803 | 68.008 | 68.919 |
| 右 | `submit_ms` | 100 | 0.047 | 0.057 | 0.052 | 0.062 | 0.078 | 0.143 | 0.144 |
| 右 | `wait_ms` | 100 | 37.527 | 59.182 | 59.822 | 61.308 | 63.442 | 67.630 | 68.566 |
| 右 | `invalidate_ms` | 100 | 0.283 | 0.337 | 0.311 | 0.322 | 0.353 | 1.227 | 1.242 |
| 右 | `release_ms` | 100 | 0.015 | 0.022 | 0.021 | 0.027 | 0.031 | 0.037 | 0.038 |

Worker 汇总（其中 `bpu_ms` 直接映射上述 `total_ms`）：

| 路 | 指标 | count | min | mean | P50 | P90 | P95 | P99 | max |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 左 | `bpu_ms` | 100 | 46.664 | 59.965 | 60.135 | 61.785 | 66.377 | 68.708 | 71.102 |
| 左 | `postprocess_ms` | 100 | 2.954 | 3.319 | 3.036 | 3.294 | 3.362 | 13.159 | 15.660 |
| 左 | `end_to_end_ms` | 100 | 50.068 | 63.424 | 63.333 | 66.609 | 70.067 | 74.630 | 77.255 |
| 右 | `bpu_ms` | 100 | 38.002 | 59.599 | 60.233 | 61.671 | 63.803 | 68.008 | 68.919 |
| 右 | `postprocess_ms` | 100 | 2.958 | 3.376 | 3.008 | 3.071 | 3.135 | 14.659 | 14.689 |
| 右 | `end_to_end_ms` | 100 | 41.590 | 63.116 | 63.364 | 65.693 | 69.322 | 75.909 | 75.942 |

运行完整性、队列和缓存统计如下；queue wait 是 100 帧期间累计值，不是逐帧均值。

| 类别 | 左 | 右 | 总体/校验 |
|---|---:|---:|---|
| 成功帧 | 100 | 100 | 200 |
| 检测框 | 113 | 100 | 213 |
| 失败 | 0 | 0 | `run_complete.errors=0` |
| VSE release error | 0 | 0 | 0 |
| 取帧 timeout | 0 | 0 | 0 |
| queue push wait 累计 | 0.068 ms | 0.036 ms | 0.104 ms |
| queue pop wait 累计 | 32.278 ms | 32.230 ms | 64.508 ms |
| cache miss / hit | 3 / 97 | 3 / 97 | 6 / 194 |
| address mismatch | 0 | 0 | 0/200 |

| 一致性项 | 结果 |
|---|---|
| 模型实例 | `model_loaded=2`，左右各 1 |
| worker 生命周期 | `worker_started=2`、`worker_stopped=2` |
| worker 处理事件 | `frame_processed=200`，左右各 100 |
| DNN 完成事件 | `inference_complete=200`，左右各 100 |
| 注册缓存清理 | `registration_cache_cleared=2` |
| pipeline 生命周期 | 每路各有一次 started/stopping/stopped；合计均为 2 |
| 结构化序列 | 423 条，sequence 1..423，无缺失、无重复、严格递增 1 |
| 可读日志一致性 | `run.log` 423 行，各事件计数与 `events.jsonl` 一致 |
| 退出状态 | `summary.exit_code=0` 且 `summary.run_complete.exit_code=0`，summary error 为空 |

## 8. 后处理耗时构成

本节只使用 `run_20260711_221544` 的 debug 事件，每路仅 2 个样本。JSON 明确记录 `percentiles_robust=false`。这里展示每个实际值和算术均值，仅用于解释处理阶段；不报告或引用其分位数，也不外推为稳定性能结论。后续取得每路至少 30 个 debug 样本，也只能用于阶段构成和均值的初步观察；若要验收 P95/P99，仍应采用每路每次至少 1000 帧并重复多次运行的主测试口径。

| 路 | 阶段 | 样本 1 | 样本 2 | 均值 |
|---|---|---:|---:|---:|
| 左 | class scan ms | 2.976 | 5.774 | 4.375 |
| 左 | DFL decode ms | 0.030 | 0.045 | 0.038 |
| 左 | map ms | 0.002 | 0.002 | 0.002 |
| 左 | NMS ms | 0.006 | 0.010 | 0.008 |
| 左 | total ms | 3.054 | 5.846 | 4.450 |
| 右 | class scan ms | 2.940 | 2.959 | 2.950 |
| 右 | DFL decode ms | 0.071 | 0.032 | 0.051 |
| 右 | map ms | 0.002 | 0.001 | 0.002 |
| 右 | NMS ms | 0.007 | 0.006 | 0.007 |
| 右 | total ms | 3.026 | 3.001 | 3.013 |

| 路 | proposals（样本 1 / 2 / 均值） | detections（样本 1 / 2 / 均值） |
|---|---:|---:|
| 左 | 10 / 11 / 10.500 | 1 / 2 / 1.500 |
| 右 | 10 / 10 / 10.000 | 1 / 1 / 1.000 |

这 4 帧中 class scan 是后处理总耗时的主要组成，DFL、map 和 NMS 较小；这是对代码阶段和样本机制的直接观察。左路第二个样本 class scan 较高，但样本数不足，不能据此断言通道固有差异或尾延迟成因。

## 9. 性能瓶颈分析

以下明确区分事实与推断。

- **测量事实：** 左右 BPU `wait_ms` 均值为 59.558/59.182 ms，而 BPU total 均值为 59.965/59.599 ms；submit、invalidate、release 的均值总和仅约 0.405/0.416 ms。故总 BPU timer 由 wait 主导。
- **工程推断：** 两路任务都提交到唯一的 `bpu_core0`，`wait_ms` 同时覆盖 runtime 排队和 BPU 执行，日志不能把二者分离。结合单 core 约束和本次首末完成区间 15.778467 pair FPS，当前吞吐瓶颈应定位在单 core 的调度/执行阶段，但不能仅凭现有埋点量化“排队”与“纯计算”的比例；该速率也不构成稳态证明。
- **并发解释：** 双模型/双 worker 不会让单 core 算力翻倍。它能隔离每路注册缓存、task 和帧所有权，并让一侧约 3.3 ms 的 CPU 后处理与另一侧的 BPU 任务调度/执行重叠，因此 aggregate 完成流可接近 paired 流的两倍。
- **后处理事实：** 100 帧 info 运行中，左右后处理均值为 3.319/3.376 ms，约为 worker 端到端均值的 5%；平均瓶颈不是 CPU 后处理。左路 postprocess P99/max 为 13.159/15.660 ms，右路为 14.659/14.689 ms；其中 P99 受最慢 1–2 个样本主导，只能提示检查原始慢样本，不能作为尾延迟认证或优化验收结论。
- **通道比较：** 左右均值和 P50 很接近；左路 BPU P95 较高（66.377 vs 63.803 ms），右路 worker 端到端 P99 较高（75.909 vs 74.630 ms）。这些是探索性差异，单次各 100 帧不足以归因于传感器、线程调度或热状态。
- **背压事实：** push 累计等待不足 0.100 ms/路，未显示队列饱和是本次瓶颈；容量 1 主要承担延迟上界与资源所有权保护，不能从低等待值推断可以无限增大队列。

## 10. 优化建议

建议按以下优先级推进；“预期验证指标”是验收方法，不是收益承诺。

| 优先级 | 建议 | 原因 | 预期验证指标 |
|---|---|---|---|
| P0 | 先建立可重复基线：相同模式多次运行，每路每次至少 1000 帧；另采集每路至少 30 帧 debug 分段；同时记录 BPU/CPU 频率、温度和功耗模式 | 当前只有一次 100 帧和每路 2 个 debug 样本 | 首末完成区间 aggregate/pair rate 的跨运行均值与离散度；延迟 P95/P99 的跨运行离散度或置信区间；debug 只初步观察阶段构成/均值；无错误、无地址 mismatch |
| P1 | 优先评估模型与 BPU runtime 路径：在精度约束下比较更轻模型/编译配置；若 SDK 提供排队与执行拆分埋点则启用 | wait 约占 BPU total 的 99%，收益空间主要在此 | 在上述 ≥1000 帧、多次运行口径下，`wait_ms` mean/P95 下降，pair intercompletion interval rate 上升；检测精度不低于业务阈值；core/address 校验仍通过 |
| P2 | 针对 CPU 后处理尾部做 profiling，检查 class scan 的数据依赖、临时分配和调度抖动，再决定是否复用缓冲或向量化 | 平均约 3.3 ms，不是首要瓶颈；当前 100 帧 P99 仅提供慢样本线索 | 先复核 P90/P95/max 对应原始样本；正式验收使用每路每次 ≥1000 帧的 postprocess mean/P95/P99，并报告多次运行离散度 |
| P3 | 增加曝光/采集、VSE 完成、入队、出队、输出就绪时间戳，并单独计量 VSE release | 现有 `end_to_end` 不是相机到结果全链路 | 可报告 exposure-to-output、VIO、queue 和 release 的独立分布，并保证日志开销经 A/B 验证可接受 |
| P4 | 保持容量 1 为默认；只有在证实 BPU 空洞由供帧抖动造成时，才 A/B 测试更深队列或调度策略 | 本次 push wait 极小，盲目扩容更可能增加陈旧帧延迟 | 多次运行的 intercompletion interval rate 改善且完整链路 P95 不恶化；内存/在途帧上限明确；release/error 仍为 0 |

## 11. 复现与诊断命令

前置条件：Docker 构建容器已运行且仓库挂载到 `/project`；X5 可通过 root SSH 访问且未被其他 VIO 应用占用；模型已部署到板端；宿主机具备 `ssh`、`scp` 和 Python 3。下面 jq 诊断块还要求板端安装 `jq`；没有 jq 时可复制日志并使用随后给出的纯 Python 替代。以下命令应在同一个宿主 shell 中执行，先设置环境变量：

```bash
export CONTAINER="${CONTAINER:-3c543521b4ca}"
export BOARD_IP="${BOARD_IP:-192.168.1.12}"
export MODEL="${MODEL:-/root/mipi_proj/model/model.bin}"
```

构建：

```bash
docker exec "$CONTAINER" bash -lc \
  'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

部署并运行同口径 100 帧测试：

```bash
cd mipi_vse_bpu_zerocopy
ssh root@"$BOARD_IP" 'mkdir -p /root/mipi_vse_bpu_zerocopy'
scp build_aarch64/mipi_vse_bpu_zerocopy scripts/run_on_board.sh \
  root@"$BOARD_IP":/root/mipi_vse_bpu_zerocopy/
ssh root@"$BOARD_IP" \
  "cd /root/mipi_vse_bpu_zerocopy && MODEL='$MODEL' FRAMES=100 LOG_LEVEL=info ./run_on_board.sh"
```

若板端已安装 jq，可查看最新 summary、地址/core 和事件计数：

```bash
ssh root@"$BOARD_IP" 'bash -s' <<'SH'
cd /root/mipi_vse_bpu_zerocopy
RUN_DIR="$(find logs -maxdepth 1 -type d -name 'run_*' | sort | tail -1)"
jq . "$RUN_DIR/summary.json"
jq -s '
  [ .[] | select(.component=="dnn" and .event=="inference_complete") ] as $d
  | {samples: ($d|length),
     address_matches: ([$d[] | select(.fields.vse_phy==.fields.dnn_phy)]|length),
     core_errors: ([$d[] | select(.fields.bpu_core_index!=0 or .fields.bpu_core_mask!=1)]|length)}
' "$RUN_DIR/events.jsonl"
jq -s 'group_by(.component+"."+.event)
  | map({event:(.[0].component+"."+.[0].event), count:length})' \
  "$RUN_DIR/events.jsonl"
SH
```

复制原始数据并用与本报告相同的线性插值方法统计延迟：

```bash
mkdir -p /tmp/mipi_vse_report_data/run_100
scp root@"$BOARD_IP":/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221831/{events.jsonl,summary.json,run.log} \
  /tmp/mipi_vse_report_data/run_100/
python3 - <<'PY'
import json, math
from collections import Counter, defaultdict

path = '/tmp/mipi_vse_report_data/run_100/events.jsonl'
events = [json.loads(line) for line in open(path)]
summary = json.load(open('/tmp/mipi_vse_report_data/run_100/summary.json'))

def percentile(values, p):
    values = sorted(values)
    pos = (len(values) - 1) * p
    lo, hi = math.floor(pos), math.ceil(pos)
    return values[lo] if lo == hi else values[lo] + (values[hi] - values[lo]) * (pos - lo)

def describe(values):
    return dict(count=len(values), min=min(values), mean=sum(values)/len(values),
                p50=percentile(values,.50), p90=percentile(values,.90),
                p95=percentile(values,.95), p99=percentile(values,.99), max=max(values))

metrics = defaultdict(lambda: defaultdict(list))
for event in events:
    key = (event['component'], event['event'])
    names = (('total_ms','submit_ms','wait_ms','invalidate_ms','release_ms')
             if key == ('dnn','inference_complete') else
             ('bpu_ms','postprocess_ms','end_to_end_ms')
             if key == ('worker','frame_processed') else ())
    for name in names:
        metrics[event['channel']][name].append(event['fields'][name])
for channel in ('left','right'):
    print(channel, json.dumps({k:describe(v) for k,v in metrics[channel].items()}, indent=2))
dnn = [e for e in events if (e['component'],e['event']) == ('dnn','inference_complete')]
print('summary', json.dumps(summary, indent=2))
print('address_matches', sum(e['fields']['vse_phy'] == e['fields']['dnn_phy'] for e in dnn), '/', len(dnn))
print('core_errors', sum(e['fields']['bpu_core_index'] != 0 or e['fields']['bpu_core_mask'] != 1 for e in dnn))
print('event_counts', Counter((e['component'],e['event']) for e in events))
PY
```

静态零拷贝门禁可在构建容器中直接执行。`build.sh` 生成的是 AArch64 测试程序，不能在 x86 构建容器中直接运行，应复制到 X5 执行：

```bash
docker exec "$CONTAINER" bash -lc '
  cd /project/mipi_vse_bpu_zerocopy
  cmake -DPROJECT_SOURCE_DIR="$PWD" -P test/check_no_image_copy.cmake
  ./build.sh
'
cd mipi_vse_bpu_zerocopy
ssh root@"$BOARD_IP" 'rm -rf /tmp/mipi_zerocopy_tests && mkdir -p /tmp/mipi_zerocopy_tests'
scp build_aarch64/test_* root@"$BOARD_IP":/tmp/mipi_zerocopy_tests/
ssh root@"$BOARD_IP" \
  'set -e; for test in /tmp/mipi_zerocopy_tests/test_*; do echo "RUN $test"; "$test"; done'
```

## 12. 结论与限制

本次测量直接证明：应用在 200 次成功推理中保持 VSE 与 DNN 输入物理地址一致，两路注册缓存行为与各 3 个 VSE buffer 相符，双 worker/双模型均提交到 `bpu_core0`，运行无 timeout、处理失败或释放错误。本次首末完成区间速率为 31.550388 aggregate FPS、15.778467 paired FPS；它不是稳态证明。BPU wait 主导约 60 ms 的 BPU total，CPU 后处理平均约 3.3 ms；因此首要优化对象是单 core 的模型执行/调度路径，而不是扩大队列或优先微调平均后处理。

结论适用范围受以下限制约束：

- 只有单次、每路 100 帧的 info 基线；首末完成区间速率虽排除首个完成前启动时间，但没有预热剔除、滚动稳定性检查或多次运行，不代表稳态、长稳、老化或不同负载下的完整性能认证。
- 100 帧 P99 几乎由最慢 1–2 个样本决定，仅供探索，不作为性能认证或优化验收；P95/P99 验收应采用每路每次至少 1000 帧、多次运行并报告跨运行离散度或置信区间。
- 后处理阶段只有每路 2 个 debug 样本，`percentiles_robust=false`，仅能解释机制；即使扩展到每路至少 30 个 debug 样本，也只用于阶段构成和均值的初步观察。
- `worker.end_to_end_ms` 从 worker 出队后开始，不包含曝光到 VSE 输出、主线程取帧和排队，也不包含 VSE frame release；它包含 processor 内同步日志入队开销，但排除异步 writer 的序列化/文件 I/O 和随后提交的 `worker.frame_processed` 事件。当前没有曝光到检测输出的真正全链路时间。
- 未记录 BPU/CPU/DDR 频率、温度、功耗模式和功耗数据，不能判断热降频、能效或功耗上限。
- `wait_ms` 合并了 BPU runtime 排队与执行，现有日志无法量化二者各自占比。
- 200/200 地址相等与静态门禁只覆盖应用可见路径，不用于宣称 SDK/驱动/硬件内部绝无复制。
- 左右尾延迟存在差异，但 100 帧样本和缺失的系统状态不足以给出因果归属；相关解释均应视为待验证的工程推断，而非测量事实。
