# RDK_proj 工程总览

本仓库面向地瓜机器人 RDK X5 平台，整理了从 YOLO 模型转换、端侧 DNN 推理封装、MIPI 双目摄像头实时推理，到 MIPI → ISP → VSE → BPU 真零拷贝、双摄硬件同步、多线程服务化推理和性能分析的完整工程样例。

当前仓库的核心目标是打通以下链路：

```text
训练模型
  -> PT/ONNX/BIN 转换
  -> RDK DNN Runtime 推理封装
  -> 单图/离线推理验证
  -> MIPI 双目摄像头实时推理
  -> ISP/VSE 硬件处理和 BPU 物理地址零拷贝绑定
  -> LPWM 双摄硬件触发和时间戳同帧配对
  -> 多路输入服务化推理
  -> 日志、可视化和性能分析
```

## 当前重点

当前重点是 `mipi_vse_bpu_zerocopy/`：在 RDK X5 上让两颗 SC132GS 以 `SLAVE_M` 接收 LPWM 30 FPS 周期触发，左右图像分别经 MIPI host 0/2、VIN、ISP 和 VSE；VSE 输出 `640×640 NV12` 连续物理缓冲，应用直接注册该物理地址并作为 BPU 输入，不执行图像复制或 CPU resize。两路各有独立 worker、模型实例、注册缓存和后处理器，共享板端唯一的 `bpu_core0`。

程序使用 VSE 帧硬件时间戳对齐左右图像，而不是按左右取帧顺序直接配对。2026-07-12 的 300 对板端验收中，左右各成功推理 300 帧，取帧超时、处理失败和零拷贝地址不一致均为 0；全部已接受 pair 的最大时间戳差为 78,750 raw tick，首末 pair 完成区间速率为 15.745 pair/s。时间戳单位和曝光电气边沿仍需 SDK 官方定义、示波器或快速闪光最终确认。

它和仓库内其他模块的关系如下：

```text
model_convert/
  负责生成 RDK 可部署的 .bin 模型

model_infer/
  封装 hbDNN 模型加载、输入预处理、BPU 推理、YOLO 后处理

mipi_vse_bpu_zerocopy/
  当前重点：SC132GS LPWM 硬件触发，MIPI -> VIN/ISP -> VSE -> BPU
  VSE 物理地址直接注册为 DNN 输入，双 worker/双模型共享 bpu_core0
  使用硬件时间戳对齐左右同曝光图像

mipi_real_time_proj/
  既有应用层实时推理方案
  基于 hobot_mipi_cam 采集 GS130W 双目 NV12 帧
  将 VideoBuffer 封装为 YoloInfer::Nv12Frame
  调用 model_infer 完成左右目实时推理

multi_thread_proj/
  基于 model_infer 做多路输入、多 worker、共享内存和 UDS 调度
```

## 目录结构

```text
RDK_proj/
  README.md                             仓库总入口
  CMakeLists.txt                        顶层 CMake，当前包含 model_infer 和 multi_thread_proj

  model_convert/                        模型转换链路
    README.md
    pt2onnx/                            PT 导出 ONNX
    onnx2bin/                           ONNX 转 RDK BIN

  model_infer/                          端侧推理基础库
    README.md
    build.sh
    model_inference_base/               RDK DNN Runtime 通用封装
    yolo_infer/                         YOLO 预处理、后处理、NV12 输入适配
    test/                               单图推理验证程序
    asset/                              测试图片和可视化示例

  mipi_vse_bpu_zerocopy/               当前重点：双摄硬件同步、VSE-BPU 真零拷贝
    README.md                           构建、部署、运行、同步和日志说明
    TECHNICAL_PERFORMANCE_REPORT.md     架构、100 帧基线和 300 对同步验收
    build.sh                            Docker AArch64 交叉构建入口
    CMakeLists.txt                      独立工程和测试目标
    include/                            VIO、帧所有权、同步配对和推理接口
    src/                                MIPI/VIN/ISP/VSE、零拷贝 BPU、双 worker
    scripts/run_on_board.sh             板端运行和日志归档入口
    test/                               零拷贝、硬件同步和资源生命周期测试

  mipi_real_time_proj/                  既有 MIPI 双目应用层实时推理工程
    README.md
    build.sh
    CMakeLists.txt
    include/                            hobot_mipi_cam 适配头、应用配置头
    src/                                camera pipeline、主程序、sensor 配置
    config/                             GS130W/其他 sensor 标定文件
    scripts/run_mipi_zero_copy_test.sh  板端测试、日志和可视化脚本
    test/                               配置解析和 NV12 视图测试

  multi_thread_proj/                    多线程服务化推理工程
    README.md
    build.sh
    CMakeLists.txt

  3rdlibrary/                           板端交叉编译依赖
    DNN/                                RDK DNN SDK 头文件和库
    opencv_aarch64/                     aarch64 OpenCV
    RDK_CAMERA/                         RDK camera SDK 头文件和库

  perf/                                 底层构建、板端执行、日志拉取脚本
  scripts/                              项目级辅助脚本
  skills/                               自建 Codex skills 和性能优化工作流
```

## 子工程职责

### 1. `model_convert`

`model_convert/` 负责模型转换，通常包括：

```text
*.pt
  -> *.onnx
  -> calibration / quantization
  -> RDK *.bin
```

转换后的 `.bin` 文件供 `model_infer`、`mipi_vse_bpu_zerocopy`、`mipi_real_time_proj` 和 `multi_thread_proj` 使用。

详细说明见 [model_convert/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_convert/README.md)。

### 2. `model_infer`

`model_infer/` 是仓库内所有推理工程复用的基础模块，主要包含：

- `ModelInferenceBase`：封装 `hbDNNInitializeFromFiles`、tensor 分配、`hbDNNInfer`、`hbDNNWaitTaskDone`、资源释放和错误信息。
- `YoloInfer`：封装 YOLO 检测模型的 BGR/NV12 输入、letterbox、输出头解析、NMS 和检测框恢复。
- `test_yolo_infer`：板端单图推理验证入口。

核心接口：

```cpp
rdk::YoloInfer infer(config, options);
infer.initialize();
infer.infer(image);
infer.infer(nv12_frame);
const auto& detections = infer.detections();
```

详细说明见 [model_infer/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/README.md)。

### 3. `mipi_vse_bpu_zerocopy`

`mipi_vse_bpu_zerocopy/` 是当前双摄硬件同步、真零拷贝推理主工程，具有独立的 `CMakeLists.txt` 和 `build.sh`，不依赖顶层 CMake 构建。主要能力包括：

- 左右 SC132GS 使用 `SLAVE_M`，由 X5 LPWM 以 30 FPS、33333 µs 周期触发。
- 两路分别使用 MIPI host 0/2，并在硬件中完成 VIN、ISP、VSE 处理。
- VSE 将中心 `1088×1088` 区域缩放为模型需要的 `640×640 NV12`。
- 使用 `hbSysRegisterMem` 注册 VSE 连续物理缓冲，DNN 输入地址与 VSE 地址保持一致。
- 左右各自使用独立 worker、模型实例、注册缓存和 YOLO11 后处理器，两个实例共享 `bpu_core0`。
- 按 SDK 硬件时间戳对齐左右帧；时间戳差超过 1,000,000 tick 时释放旧帧并重取，避免把相邻曝光周期误配为一对。
- 输出文本日志、JSONL、summary、零拷贝地址证据、同步偏差和完整性能指标。

核心数据流：

```text
X5 LPWM
  -> left/right SC132GS SLAVE_M
  -> MIPI host 0/2
  -> VIN -> ISP -> VSE 640x640 NV12
  -> hardware timestamp stereo alignment
  -> hbSysRegisterMem(VSE physical buffer)
  -> two model instances / two workers
  -> shared bpu_core0
  -> YOLO11 CPU postprocess
  -> VseFrame release
```

本工程“零拷贝”的准确边界是：应用直接把 VSE 输出物理缓冲注册为 DNN 输入，源码门禁禁止推理路径使用 `hbSysWriteMem`、图像 `memcpy` 和 OpenCV resize；这证明应用可见路径没有显式图像复制，不用于宣称 SDK、驱动或硬件内部绝无复制。

详细说明见 [mipi_vse_bpu_zerocopy/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/mipi_vse_bpu_zerocopy/README.md)，技术与性能数据见 [TECHNICAL_PERFORMANCE_REPORT.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md)。

### 4. `mipi_real_time_proj`

`mipi_real_time_proj/` 是既有的实时摄像头应用层推理工程。它基于已下载并导入本仓库的 `hobot_mipi_cam` 代码，完成：

- GS130W 双目摄像头初始化。
- MIPI host `0/2` 双路采集。
- `left/right/combine` 通道取帧。
- `VideoBuffer::buff` 到 `YoloInfer::Nv12Frame` 的应用层零拷贝封装。
- 调用 `model_infer` 执行 YOLO 推理。
- 输出检测结果、链路耗时、JSONL、summary 和 JPEG 可视化。

默认硬件配置：

```text
board_id: 302
sensor: sc132gs-1280p
device_mode: dual
channel: 0
channel2: 2
infer_channels: left,right
image_width: 1088
image_height: 1280
out_format: nv12
model: ~/mipi_proj/model/model.bin
calibration: ~/mipi_proj/config/SC132gs_dual_calibration.yaml
```

核心数据流：

```text
GS130W left/right sensor
  -> MIPI host 0/2
  -> VIN/ISP/VSE
  -> hbn_vnode_image_t
  -> VideoBuffer::buff
  -> makeNv12FrameView()
  -> rdk::YoloInfer::infer(Nv12Frame)
  -> detections()
  -> log / JSONL / summary / visualization
```

注意当前“零拷贝”的准确边界：

- `hobot_mipi_cam` 内部从 HBN VIO 输出到 `VideoBuffer::buff` 仍有一次 `memcpy`。
- `mipi_real_time_proj` 应用层从 `VideoBuffer::buff` 到 `YoloInfer::Nv12Frame` 不再额外复制图像。
- `model_infer` 预处理阶段会把 NV12 数据写入 DNN 输入 tensor，这是 BPU 执行前必要的数据准备。

详细说明见 [mipi_real_time_proj/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/mipi_real_time_proj/README.md)。

### 5. `multi_thread_proj`

`multi_thread_proj/` 在 `model_infer` 的单模型能力之上，提供多路输入服务化封装：

- `mt_manager` 扫描输入、准备批次并通过共享内存投递负载。
- `mt_server` 接收通知、分发 worker、执行推理并聚合检测结果。
- 管理端和服务端通过 Unix Domain Socket 传控制消息，通过共享内存传图像数据。

详细说明见 [multi_thread_proj/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/README.md)。

## 推荐使用路径

### 路径 A：先验证模型推理

适合确认 `.bin` 模型、DNN Runtime、OpenCV 和 YOLO 后处理是否正常。

```bash
cd /project/model_infer
./build.sh
```

板端运行示例：

```bash
./test_yolo_infer \
  --model /path/to/model.bin \
  --image /path/to/test.jpg \
  --nv12 \
  --output result.jpg \
  --score 0.25 \
  --nms 0.45 \
  --classes 80
```

### 路径 B：运行双摄硬件同步、VSE-BPU 真零拷贝推理

适合验证当前主链路：LPWM 同参数触发两颗 SC132GS，MIPI/VIN/ISP/VSE 硬件处理，VSE 物理缓冲直接注册给 BPU，并按硬件时间戳配对左右图像。

编译：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

部署和板端运行：

```bash
cd mipi_vse_bpu_zerocopy
ssh root@192.168.1.12 'mkdir -p /root/mipi_vse_bpu_zerocopy'
scp build_aarch64/mipi_vse_bpu_zerocopy scripts/run_on_board.sh \
  root@192.168.1.12:/root/mipi_vse_bpu_zerocopy/
ssh root@192.168.1.12 \
  'cd /root/mipi_vse_bpu_zerocopy && FRAMES=300 LOG_LEVEL=info ./run_on_board.sh'
```

每次运行会生成 `run.log`、`events.jsonl`、`console.log` 和 `summary.json`。同步最大偏差及左右主动对齐丢帧数记录在 `events.jsonl` 的 `summary.run_complete` 事件中。

### 路径 C：运行既有 MIPI 应用层实时推理

适合通过 `hobot_mipi_cam + model_infer` 验证从采集、应用层 NV12 视图到推理和 JPEG 可视化的既有链路。该工程的零拷贝边界与 `mipi_vse_bpu_zerocopy` 不同。

编译：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_real_time_proj && ./build.sh'
```

板端运行：

```bash
cd /project/mipi_real_time_proj/build_aarch64
export LD_LIBRARY_PATH=/project/3rdlibrary/RDK_CAMERA/lib:/project/3rdlibrary/DNN/lib:/project/3rdlibrary/opencv_aarch64/lib:$LD_LIBRARY_PATH

./mipi_zero_copy_infer \
  --model ~/mipi_proj/model/model.bin \
  --frames 100 \
  --output-dir ~/mipi_proj/run_001 \
  --log-jsonl \
  --save-vis \
  --save-every 5 \
  --vis-rotate 90
```

推荐使用测试脚本：

```bash
cd /project/mipi_real_time_proj
FRAMES=100 SAVE_EVERY=5 VIS_ROTATE=90 ./scripts/run_mipi_zero_copy_test.sh
```

输出目录包含：

```text
run.log          完整 stdout/stderr
frames.jsonl     每帧结构化耗时和检测框
summary.txt      每通道统计汇总
vis/             左右目 JPEG 可视化
```

### 路径 D：多线程服务化推理

适合离线多路图片或视频帧输入、批量调度和多 worker 服务化验证。

```bash
cd /project/multi_thread_proj
./build.sh
```

详细运行方式见 [multi_thread_proj/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/README.md)。

## 编译环境

### 通用依赖

- CMake 3.14+
- `aarch64-linux-gnu-gcc`
- `aarch64-linux-gnu-g++`
- RDK DNN SDK
- aarch64 OpenCV（`model_infer`、`mipi_real_time_proj` 使用；`mipi_vse_bpu_zerocopy` 核心链路不依赖 OpenCV）

### MIPI 摄像头工程额外依赖

- RDK camera SDK 头文件和库
- `hbn_api.h`
- `hb_camera_interface.h`
- `vin_cfg.h`
- `hb_media_codec.h`
- `hb_mem_mgr.h`
- GS130W 对应 sensor 配置和标定文件

本仓库默认依赖目录：

```text
3rdlibrary/DNN
3rdlibrary/opencv_aarch64
3rdlibrary/RDK_CAMERA
```

如果 SDK 不在默认路径，可以通过各子工程 `build.sh` 的环境变量覆盖。

## 常用命令

### 构建 `model_infer`

```bash
cd model_infer
./build.sh
```

### 构建 `mipi_vse_bpu_zerocopy`

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

### 检查硬件同步和零拷贝静态门禁

```bash
docker exec 3c543521b4ca bash -lc '
  cd /project/mipi_vse_bpu_zerocopy
  cmake -DPROJECT_SOURCE_DIR="$PWD" -P test/check_hardware_sync_config.cmake
  cmake -DPROJECT_SOURCE_DIR="$PWD" -P test/check_no_image_copy.cmake
'
```

### 构建 `mipi_real_time_proj`

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_real_time_proj && ./build.sh'
```

### 只编译 MIPI 配置测试

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_real_time_proj && ./build.sh --target test_mipi_zero_copy_config'
```

### 构建 `multi_thread_proj`

```bash
cd multi_thread_proj
./build.sh
```

## 板端检查命令

MIPI 摄像头不出图，或 `getFrame`/`hbn_vnode_getframe` 失败时，优先在板端检查：

```bash
cat /sys/class/socinfo/board_id
cat /etc/board_config.json
ls /dev/i2c-*
```

对当前 GS130W 双目配置，`board_id=302` 时通常使用：

```text
channel=0
channel2=2
```

`mipi_vse_bpu_zerocopy` 默认直接使用 MIPI host 0/2，并从设备树读取每路 `rx_phy`、`lpwm_chn`、GPIO 和 I2C 路由；初始化失败时应结合 `run.log`、`console.log` 和内核日志检查传感器 ID、LPWM/FSIN 连接及 VIO 资源占用。

## 文档导航

- [model_convert/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_convert/README.md)：模型导出和 RDK BIN 转换。
- [model_infer/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/README.md)：DNN 推理基础库、YOLO 推理接口、单图验证。
- [mipi_vse_bpu_zerocopy/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/mipi_vse_bpu_zerocopy/README.md)：当前双摄 LPWM 硬件同步、MIPI/VIN/ISP/VSE、BPU 真零拷贝工程。
- [mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md)：架构、指标口径、100 帧基线和 300 对同步验收。
- [mipi_real_time_proj/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/mipi_real_time_proj/README.md)：GS130W 双目 MIPI 实时推理、数据流、日志和可视化。
- [multi_thread_proj/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/README.md)：多路输入、多 worker、共享内存服务化推理。
- [skills/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/skills/README.md)：自建 skill 和性能优化工作流。

## 维护建议

- 修改 `model_infer` 的推理接口时，同步检查 `mipi_real_time_proj` 和 `multi_thread_proj` 的调用。
- 修改 MIPI 默认 camera 参数时，同步更新 `mipi_real_time_proj/README.md`、`defaultConfig()` 和 `test_mipi_zero_copy_config.cpp`。
- 修改 `mipi_vse_bpu_zerocopy` 的帧率、sensor mode 或 LPWM 参数时，同步更新其 README、技术性能报告和 `check_hardware_sync_config.cmake`，并重新执行至少 300 对板端同步验收。
- 修改 VSE 输出布局或 DNN 输入绑定时，必须保持 `no_image_copy`、地址一致性日志和 11 个板端测试通过，禁止静默退化到 CPU copy/resize。
- 新增模型时，先用 `model_infer/test_yolo_infer` 验证单图结果，再接入 MIPI 或多线程工程。
- 板端性能问题按工程查看日志：`mipi_vse_bpu_zerocopy` 使用 `run.log`、`events.jsonl`、`console.log` 和 `summary.json`；`mipi_real_time_proj` 使用 `run.log`、`frames.jsonl` 和 `summary.txt`。分析时区分取帧/同步、VSE、BPU、后处理和可视化开销。
