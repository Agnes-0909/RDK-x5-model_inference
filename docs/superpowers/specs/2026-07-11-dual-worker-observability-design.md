# 双路独立推理线程与全链路日志系统设计

## 目标

在现有 `mipi_vse_bpu_zerocopy` 真零拷贝工程上完成两个关联改造：

1. 建立统一的异步日志系统，记录从程序启动、sensor/VIO 初始化、VSE 取帧、零拷贝内存注册、BPU 推理、CPU 后处理到资源释放的完整过程。
2. 将左右路改为两个独立 worker。每路拥有自己的模型实例、推理线程、后处理状态、内存注册缓存和统计；VSE 帧进入对应 worker 后，只有该路推理和后处理完成才归还硬件帧。

工程继续保持输入零拷贝，不引入第三方日志库。

## 已确认的硬件约束

板端只存在一个 BPU 核心：

```text
/sys/class/misc/bpu_core0
/sys/devices/system/bpu/bpu0
```

因此左右两个模型实例都固定提交到 `HB_BPU_CORE_0`。两个线程可以并发提交任务，但 BPU 计算由单核心 runtime 调度。线程化的收益是左右生命周期隔离、CPU 后处理与另一侧 BPU 调度重叠，以及更完整的可观测性，不以 BPU 总吞吐翻倍为目标。

## 方案选择

采用工程内置异步日志器。业务线程只构造事件并写入内存队列，单独日志线程负责终端、文本日志、JSONL、flush 和轮转。

未采用同步互斥写文件，因为磁盘 I/O 会直接进入采集或推理延迟。未采用 spdlog，因为当前独立工程没有该 aarch64 依赖，引入它会扩大构建和部署范围。

## 并发架构

```text
采集主线程
  ├─ acquire left VseFrame
  │    └─ move -> left queue（容量 1）
  └─ acquire right VseFrame
       └─ move -> right queue（容量 1）

left-worker
  ├─ 独立 ZeroCopyYoloRunner
  ├─ 独立 RegisteredBufferCache
  ├─ HB_BPU_CORE_0 infer/wait
  ├─ 独立 YOLO11 后处理
  ├─ 左路统计和日志
  └─ release left VseFrame

right-worker
  ├─ 独立 ZeroCopyYoloRunner
  ├─ 独立 RegisteredBufferCache
  ├─ HB_BPU_CORE_0 infer/wait
  ├─ 独立 YOLO11 后处理
  ├─ 右路统计和日志
  └─ release right VseFrame

async-logger
  ├─ 终端
  ├─ run.log
  ├─ events.jsonl
  └─ 文件轮转
```

### 采集与背压

每路使用容量为 1 的阻塞队列。已经从 VSE 取得的帧绝不因队列满而丢弃；生产者等待对应 worker 取走旧任务。每路最多同时持有一帧正在处理、一帧等待处理。

队列传递 `VseFrame` 的移动所有权，不复制图像或 frame guard。worker 完成 BPU wait、输出 cache invalidate、后处理、统计和日志事件提交后，显式归还帧。异常路径由 RAII 保证至多归还一次。

队列关闭必须同时唤醒阻塞的生产者和消费者。`push()` 在关闭或共享停止状态生效后返回失败，调用方保留的 `VseFrame` 自动归还。

### Worker 所有权

`ChannelWorker` 明确拥有：

- channel 名称和 worker 线程。
- 一个容量为 1 的 `BoundedFrameQueue`。
- 一个 `ZeroCopyYoloRunner` 模型实例。
- 一个 `RegisteredBufferCache`。
- 该路独占的 YOLO 后处理对象和检测结果。
- 该路统计计数器。

两个 runner 顺序初始化，避免并发模型加载依赖未验证的 SDK 线程安全。初始化完成后再启动 worker 和 VIO 采集。runner 的 `hbDNNInferCtrlParam.bpuCoreId` 显式设为 `HB_BPU_CORE_0`。

### 共享停止状态

主线程和两个 worker 共享协调状态，保存原子停止标志和第一个致命错误。任何 worker 失败时：

1. 原子设置停止。
2. 保存首个错误及 channel/frame 上下文。
3. 关闭左右队列，唤醒所有阻塞操作。
4. 采集线程停止取得新帧。
5. 已进入 BPU 的任务等待完成或 SDK timeout；其他帧由 RAII 归还。

## 日志组件

### AsyncLogger

日志器拥有一个有界事件队列和一个 writer 线程。`log()` 是线程安全入口，为事件分配全局递增 sequence 后入队。队列达到上限时阻塞调用者，不静默丢弃日志，以满足完整过程记录要求。

日志器启动早于模型和 VIO，停止晚于 summary 和资源释放。`shutdown()` 关闭事件队列，writer 写完全部剩余事件、flush 文件后退出。

### LogEvent

每条事件包含：

```text
wall_time        带时区的 ISO-8601 时间
monotonic_us     程序启动后的单调时钟微秒数
sequence         全局唯一递增序号
level            debug/info/warn/error
thread           main/left-worker/right-worker/logger
component        app/sensor/vio/queue/memory/dnn/postprocess/summary
event            稳定的机器可读事件名
channel          left/right/空
frame_id         帧事件使用，其他事件为空
message          人类可读描述
fields           字符串、整数、浮点和布尔扩展字段
```

JSON 序列化在工程内实现字符串转义，保持数值和布尔类型，不依赖 jsoncpp。

### 输出文件

每次运行创建独立目录：

```text
logs/run_YYYYMMDD_HHMMSS/
  run.log
  events.jsonl
  console.log
  summary.json
```

- `run.log`：应用生成的易读文本事件。
- `events.jsonl`：与文本事件一一对应的结构化记录。
- `console.log`：由 `run_on_board.sh` 捕获应用及 HBN/DNN SDK 的 stdout/stderr。
- `summary.json`：worker join 后写入的最终统计。

`run.log` 和 `events.jsonl` 分别按 100 MB 轮转，后缀为 `.1` 至 `.5`，最多保留五份历史文件。支持 `--log-dir`、`--log-level debug|info|warn|error`、`--log-max-mb` 和 `--log-files`；默认值分别为 `logs`、`info`、100、5。

### 日志覆盖范围

- `app`：版本、完整启动配置、信号、停止来源、退出码。
- `sensor`：vcon、I²C bus、候选地址、chip ID、选中地址。
- `vio`：camera/VIN/ISP/VSE 创建、属性、buffer、绑定、启动、停止和销毁。
- `queue`：入队、出队、队列深度、阻塞耗时、关闭和剩余帧。
- `memory`：plane 布局、VSE 地址、首次注册、缓存命中、注销。
- `dnn`：实例名、模型加载、tensor 属性、core 0、submit、wait、invalidate 和任务释放。
- `postprocess`：类别扫描、DFL、坐标恢复、NMS、候选数、检测数和分段耗时。
- `summary`：每路成功、超时、检测、队列等待、BPU、后处理、端到端、buffer cache 和地址一致性。

逐帧 `info` 日志保留当前零拷贝证据和总体耗时。更细的 VSE get/release、queue push/pop 和后处理分段使用 `debug`，避免默认日志量过大。

## 初始化和退出顺序

```text
启动 AsyncLogger
  -> 顺序初始化 left/right runner
  -> 初始化双路 VIO
  -> 启动 left/right worker
  -> 主线程采集并投递
  -> 停止采集并关闭两个队列
  -> worker 处理完已入队帧并 join
  -> worker 注销各自 RegisteredBufferCache
  -> 停止并销毁 VIO
  -> 写 summary 事件和 summary.json
  -> flush 并停止 AsyncLogger
```

模型实例必须在线程 join 后释放。VIO buffer pool 必须在两个注册缓存全部注销后销毁。日志器必须覆盖全部释放过程。

## 运行脚本

保留用户当前对 `scripts/run_on_board.sh` 的未提交修改和现有参数风格。在此基础上：

- 创建带时间戳的运行目录。
- 把该目录作为 `--log-dir` 传给应用。
- 通过 `tee` 捕获 stdout/stderr 到 `console.log`。
- 使用 `set -o pipefail` 保留应用真实退出码。
- 支持环境变量覆盖 `MODEL`、`FRAMES`、`SCORE`、`NMS`、`LOG_ROOT` 和 `LOG_LEVEL`。

## 测试策略

实现遵循测试先行。宿主测试覆盖：

- 多线程日志 sequence 唯一且递增。
- 文本和 JSONL 事件数量一致，JSON 转义和类型正确。
- 日志等级过滤和 100 MB 参数化轮转逻辑。
- shutdown 写完队列尾部事件。
- 容量 1 队列的阻塞、唤醒、关闭和 move-only frame 所有权。
- 正常、worker 失败和退出信号路径中帧只归还一次。
- 左右 worker 使用不同 runner/cache 实例，统计不串路。
- 两个 runner 均设置 `HB_BPU_CORE_0`。
- 保持输入路径无 `memcpy`、`hbSysWriteMem` 和 CPU resize。

X5 板端验证左右各 100 帧：

- 已取帧丢弃数为 0。
- 日志存在 left-worker 和 right-worker 的交错事件。
- 模型加载和释放各出现两次。
- 200 帧全部 `vse_phy == dnn_phy`。
- 每路各首次注册三个 VSE buffer，后续缓存命中。
- `run.log`、`events.jsonl`、`console.log`、`summary.json` 存在且相互一致。
- JSONL 全部可解析，错误计数为 0，进程退出码为 0。

## 验收标准

- 两路分别由独立线程、独立模型、独立后处理和独立注册缓存处理。
- 两个实例只使用板端存在的 `bpu_core0`。
- VSE 帧从取出到该路后处理完成期间始终由同一任务拥有，不提前释放。
- 队列满时阻塞，不丢弃已经取得的帧。
- 完整过程同时生成易读文本、结构化 JSONL 和 SDK console 日志。
- 日志轮转、等级、flush 和异常退出行为经过自动测试。
- 双路 100 帧板端验收通过，零拷贝证据保持不变。
