# mipi_vse_bpu_zerocopy

RDK X5 GS130W 双目真零拷贝推理工程。左右 SC132GS 图像分别经过 MIPI host 0/2、VIN、ISP 和 VSE；VSE 把中心 `1088x1088` 区域硬件缩放成模型需要的 `640x640 NV12`。程序注册 VSE 缓冲区物理地址并直接作为 BPU 输入，不复制图像数据。左右路各有独立 worker、模型实例、注册缓存和后处理器；两个实例都提交到板端唯一的 `bpu_core0`。

## 构建

仓库在 Docker `3c543521b4ca` 的 `/project` 下挂载时：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

本功能分支的隔离工作树路径为：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/.worktrees/dual-worker-observability/mipi_vse_bpu_zerocopy && ./build.sh'
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

## 日志与统计

每次运行建立 `logs/run_YYYYMMDD_HHMMSS/`，其中：

- `run.log`：应用生成的单行可读事件。
- `events.jsonl`：与 `run.log` 一一对应的结构化事件。
- `console.log`：脚本捕获的应用以及 HBN/DNN SDK stdout/stderr。
- `summary.json`：左右路帧数、检测数、地址一致性、缓存和耗时汇总。

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

## 常见问题

- `hbSysRegisterMem failed`：确认板端 DNN Runtime 与 SDK 匹配，并检查 VSE 输出物理地址和长度。
- `VSE plane byte sizes` 或 `stride` 错误：查看当前板端 VPF 的 buffer 对齐方式；工程有意不做复制兜底。
- `hbn_camera_create` 或 VIN 初始化失败：确认 GS130W 已连接到 host 0/2，且没有其他进程占用 camera/VIO。
- 连续取帧超时：检查 MIPI 排线、电源、传感器时钟和内核日志。
