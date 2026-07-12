# mipi_vse_bpu_zerocopy

RDK X5 GS130W 双目硬件同步、真零拷贝推理工程。X5 LPWM 持续触发两颗 SC132GS 曝光；左右图像分别经过 MIPI host 0/2、VIN、ISP 和 VSE，VSE 把中心 `1088x1088` 区域硬件缩放成模型需要的 `640x640 NV12`。程序按硬件时间戳对齐左右帧，注册 VSE 缓冲区物理地址并直接作为 BPU 输入，不复制图像数据。左右路各有独立 worker、模型实例、注册缓存和后处理器；两个实例都提交到板端唯一的 `bpu_core0`。

详细架构、指标定义、100 帧性能基线和 300 对同步验收见 [TECHNICAL_PERFORMANCE_REPORT.md](TECHNICAL_PERFORMANCE_REPORT.md)。

## 构建

仓库在 Docker `3c543521b4ca` 的 `/project` 下挂载时：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

输出为 `build_aarch64/mipi_vse_bpu_zerocopy`。

## 部署和运行

```bash
ssh root@192.168.1.12 'mkdir -p /root/mipi_vse_bpu_zerocopy'
scp build_aarch64/mipi_vse_bpu_zerocopy scripts/run_on_board.sh \
  root@192.168.1.12:/root/mipi_vse_bpu_zerocopy/
ssh root@192.168.1.12 \
  'cd /root/mipi_vse_bpu_zerocopy && FRAMES=100 ./run_on_board.sh'
```

默认模型是 `/root/mipi_proj/model/model.bin`，左右各推理 100 帧，score/NMS 分别为 0.25/0.45。`FRAMES=0` 表示持续运行。脚本支持以下覆盖：

```bash
MODEL=/root/mipi_proj/model/model.bin \
FRAMES=2 SCORE=0.25 NMS=0.45 \
LOG_ROOT=/root/mipi_vse_bpu_zerocopy/logs LOG_LEVEL=debug \
./run_on_board.sh
```

还可用 `LOG_MAX_MB` 和 `LOG_FILES` 调整轮转，默认每个日志文件 100 MB、保留 5 份。应用参数可通过 `./mipi_vse_bpu_zerocopy --help` 查看。

## 双摄硬件同步

左右 SC132GS 均工作在 `SLAVE_M`，由各自相机接口路由的 X5 LPWM 通道以相同参数持续触发曝光：30 FPS、周期 33333 µs、offset 10 µs、脉宽 100 µs。ISP 仍使用 `ISP_NORMAL_M`。程序会先完成左右两路 VIO flow 的全部配置，再启动两路 flow；推理和后处理仍保持流水执行，不等待上一对图像处理结束后再曝光。

由于两路推理共享一个 BPU 核，处理速度低于 30 FPS 相机帧率，VSE 取帧时可能跳过不同数量的帧。程序不会按左右取帧顺序直接配对，而是要求硬件时间戳差不超过 1,000,000 tick；超过阈值时释放时间戳较旧的一路并重新取帧。`left_sync_drops` 和 `right_sync_drops` 是为恢复同曝光配对而主动丢弃的帧数，不是推理失败。

每 30 对图像会记录一次 `sync.stereo_sync_sample`，字段包括左右 frame id、原始硬件时间戳、`skew_ticks` 和运行期间 `max_skew_ticks`：

```bash
RUN_DIR="$(find logs -maxdepth 1 -type d -name 'run_*' | sort | tail -1)"
jq -c 'select(.component == "sync" and .event == "stereo_sync_sample")' \
  "${RUN_DIR}/events.jsonl"
jq -c 'select(.component == "summary" and .event == "run_complete")' \
  "${RUN_DIR}/events.jsonl"
```

`skew_ticks` 是 SDK 返回的原始时间戳 tick；在板端确认时钟单位之前，不应直接解释为微秒。软件配置和时间戳接近只能作为同步证据之一。GS130W 必须把两颗传感器的外部触发脚接到相应 LPWM，且两个 LPWM 通道必须同相。如果两个通道存在稳定相位差，应将一个硬件触发信号物理扇出到两颗传感器，并用示波器或同时照射两颗传感器的快速闪光再次确认曝光同步。

2026-07-12 在 `root@192.168.1.12` 连续运行 300 对的实测结果如下：

| 指标 | 左 | 右 | 总体 |
|---|---:|---:|---:|
| 成功推理 | 300 | 300 | 600 |
| BPU 平均耗时 | 58.663 ms | 59.524 ms | — |
| 后处理平均耗时 | 3.140 ms | 3.298 ms | — |
| worker 端到端平均耗时 | 61.928 ms | 62.948 ms | — |
| 同步主动丢帧 | 17 | 253 | 270 |
| 取帧超时 | 0 | 0 | 0 |
| 处理失败 | 0 | 0 | 0 |
| 零拷贝地址不一致 | 0 | 0 | 0 |

全部 300 对的最大配对时间戳差为 78,750 tick；首末 pair 完成区间速率为 15.745 pair/s。左右主动丢帧数不对称来自主线程顺序取帧和单 BPU core 的处理反压，它表示为恢复同曝光配对而释放的旧帧，不是相机或推理错误。该结果证明软件时间戳配对稳定；曝光电气边沿是否完全同步仍以示波器或快速闪光测试为最终依据。

## 日志与统计

每次运行建立 `logs/run_YYYYMMDD_HHMMSS/`，其中：

- `run.log`：应用生成的单行可读事件。
- `events.jsonl`：与 `run.log` 一一对应的结构化事件。
- `console.log`：脚本捕获的应用以及 HBN/DNN SDK stdout/stderr。
- `summary.json`：左右路帧数、检测数、地址一致性、缓存和耗时汇总。

同步最大偏差及主动丢帧数记录在 `events.jsonl` 的 `summary.run_complete` 事件中；当前 `summary.json` 不包含这三个同步字段。

常用检查：

```bash
RUN_DIR="$(find logs -maxdepth 1 -type d -name 'run_*' | sort | tail -1)"
jq . "${RUN_DIR}/summary.json"
jq -c 'select(.component == "dnn" and .event == "inference_complete")' \
  "${RUN_DIR}/events.jsonl"
jq -s 'group_by(.channel) | map({channel: .[0].channel, events: length})' \
  "${RUN_DIR}/events.jsonl"
```

## 零拷贝证据

每次 `dnn.inference_complete` 事件同时记录 `vse_phy` 和 `dnn_phy`，两者必须完全相等。`memory.memory_registered` 只会在每路 VSE 缓冲池中首次出现某个物理地址时出现，之后记录 `memory.registration_cache_hit`。工程的 `no_image_copy` 测试禁止推理路径使用 `hbSysWriteMem`、图像 `memcpy` 或 OpenCV resize。

如果 VSE 输出不是严格的 640×640、stride 640、两 plane 物理连续 NV12，程序会退出并给出具体原因，不会退化到复制路径。

## 测试

`build.sh` 生成 AArch64 测试程序，需放到 X5 运行。静态门禁在构建容器中执行：

```bash
docker exec 3c543521b4ca bash -lc '
  cd /project/mipi_vse_bpu_zerocopy
  cmake -DPROJECT_SOURCE_DIR="$PWD" -P test/check_hardware_sync_config.cmake
  cmake -DPROJECT_SOURCE_DIR="$PWD" -P test/check_no_image_copy.cmake
  ./build.sh
'

ssh root@192.168.1.12 'rm -rf /tmp/mipi_zerocopy_tests && mkdir -p /tmp/mipi_zerocopy_tests'
scp build_aarch64/test_* root@192.168.1.12:/tmp/mipi_zerocopy_tests/
ssh root@192.168.1.12 \
  'set -e; for test in /tmp/mipi_zerocopy_tests/test_*; do "$test"; done'
```

当前共 11 个板端测试，其中 `test_stereo_frame_sync` 验证时间戳配对决策，`hardware_sync_config` 验证 30 FPS、`SLAVE_M`、LPWM enabled 和双 flow prepare/start 顺序。

## 常见问题

- `hbSysRegisterMem failed`：确认板端 DNN Runtime 与 SDK 匹配，并检查 VSE 输出物理地址和长度。
- `VSE plane byte sizes` 或 `stride` 错误：查看当前板端 VPF 的 buffer 对齐方式；工程有意不做复制兜底。
- `hbn_camera_create` 或 VIN 初始化失败：确认 GS130W 已连接到 host 0/2，且没有其他进程占用 camera/VIO。
- 连续取帧超时：检查 MIPI 排线、电源、传感器时钟和内核日志。
