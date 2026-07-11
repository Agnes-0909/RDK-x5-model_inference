# MIPI–VSE–BPU 真零拷贝工程设计

## 目标

在 `mipi_vse_bpu_zerocopy/` 中建立一个独立的 RDK X5 C++ 工程，打通 GS130W 双目相机的两条实时链路：

```text
SC132GS sensor
  -> MIPI host 0/2
  -> VIN
  -> ISP
  -> VSE 中心裁剪并缩放
  -> 注册 VSE 输出物理内存
  -> BPU YOLO11m 推理
  -> 检测后处理和性能统计
```

VSE 输出缓冲区直接成为 DNN 输入 tensor。应用不复制图像、不执行 CPU resize，也不在失败时静默退化到复制路径。

## 已确认的软硬件基线

- 开发板：RDK X5，board ID `0x302`，登录地址 `root@192.168.1.12`。
- 相机：GS130W 双目，传感器为 SC132GS。
- 左右 MIPI host：左路 `0`，右路 `2`。
- Sensor 输出：每路 `1088x1280`、RAW10、30 FPS。
- 模型：板端 `/root/mipi_proj/model/model.bin`。
- 模型名：`yolo11m_bayese_640x640_nv12`。
- 模型输入：`HB_DNN_INPUT_FROM_PYRAMID`、`HB_DNN_IMG_TYPE_NV12`、NCHW、`1x3x640x640`，输入字节数 `614400`。
- 板端 DNN Runtime：1.24.5，HBRT 3.15.55.0。
- 两路图像都持续推理，共用一个模型实例并顺序提交 BPU 任务。

## 零拷贝边界

本工程采用 VSE 自有缓冲池。`hbn_vnode_getframe()` 返回的 `hbn_vnode_image_t` 保持有效，直至对应 BPU 任务完成。

对每个首次出现的 VSE Y-plane 物理地址，程序构造 `hbSysMem` 并调用 `hbSysRegisterMem()`。注册后的描述符被缓存，DNN 输入 tensor 的 `sysMem[0]` 直接引用该描述符。缓存条目只描述外部 VSE 内存，不拥有内存，因此退出时调用 `hbSysUnregisterMem()`，绝不调用 `hbSysFreeMem()`。

运行时必须同时满足以下条件：

- 输出格式为 NV12，宽高为 `640x640`。
- Y 和 UV stride 均为 640；不存在需要逐行压紧的 padding。
- Y plane 为 `409600` 字节，UV plane 为 `204800` 字节。
- `uv_phys_addr == y_phys_addr + 409600`，两个 plane 构成一段连续物理内存。
- 连续内存总长度不少于 `614400` 字节。
- DNN tensor 输入物理地址等于当前 VSE Y-plane 物理地址。

任一条件不满足即输出明确诊断并停止。实现中不得出现图像数据的 `memcpy`、`hbSysWriteMem`、OpenCV resize 或复制兜底。

虽然当前 SDK 将 `hbSysRegisterMem()` 标为兼容接口，其头文件仍明确说明该接口用于将 VIO 等外部内存交给 BPU，且板端运行库提供该符号，因此本工程按用户要求使用它。

## 图像几何

SC132GS 图像为竖向 `1088x1280`。为避免把非正方形图像拉伸变形，VSE 对左右两路执行相同的中心裁剪：

```text
ROI: x=0, y=96, width=1088, height=1088
VSE target: 640x640 NV12
scale: 640 / 1088
```

后处理先将模型坐标乘以 `1088 / 640`，再将纵坐标加 96，最终裁剪到原图 `[0,1088] x [0,1280]`。输出检测框始终使用原始相机坐标。

## 组件

### DualVioPipeline

创建左右两套 `camera -> VIN -> ISP -> VSE` VFlow，分别固定到 MIPI host 0 和 2。每套 VSE 只开启一个输出通道，配置中心 ROI 和 `640x640` NV12 输出，并使用至少三个连续缓冲区。

该组件负责节点的创建、绑定、启动、停止和逆序销毁，不涉及 DNN。

### VseFrame

`VseFrame` 是不可复制、可移动的 RAII 句柄，持有 vnode handle、channel ID 和完整的 `hbn_vnode_image_t`。析构或显式 `release()` 时调用 `hbn_vnode_releaseframe()`，保证正常、错误和退出路径都不会泄漏硬件帧。

### RegisteredBufferCache

缓存键为 VSE Y-plane 物理地址。首次遇到地址时验证 buffer 布局并注册，随后返回稳定的 `hbSysMem` 描述符。命中缓存时还要核对虚拟地址和长度未改变，避免错误复用。

组件提供显式 `clear()`，在 VSE buffer pool 仍然有效时注销所有条目。注销失败会记录全部地址和错误码，但继续尝试注销其余条目。

### ZeroCopyYoloRunner

加载模型并验证输入属性与已确认基线一致。它只分配和管理输出 tensor；输入 tensor 不分配 buffer。

每次推理时，runner 接收已注册的 VSE 内存描述符，将其赋给临时输入 tensor，提交 `hbDNNInfer()`，等待 `hbDNNWaitTaskDone()`，释放 task handle，invalidate 输出 tensor，然后执行 YOLO11 多尺度输出解析和 NMS。调用返回前，BPU 已不再读取输入帧。

### DetectionMapper

将 640 坐标系中的检测框转换回 `1088x1280` 原图坐标。该逻辑不依赖硬件，可独立单元测试。

### Application

主循环先取得左、右两帧，再依次执行左路和右路推理。持有两个 RAII 帧可保证两路来自相近采集周期，同时单个模型实例不会被并发访问。

每帧输出通道、frame ID、VSE 与 DNN 物理地址、注册缓存状态、BPU 耗时、后处理耗时、端到端耗时和检测结果。结束时分别汇总左右路成功数、超时数、平均耗时和检测数。

## 错误处理和资源生命周期

所有 camera、HBN、hbmem 和 DNN API 返回值都必须检查。错误包含操作名、左右通道、frame ID 和 SDK 返回码。

初始化失败时，仅释放已经成功创建的资源，并严格逆序回滚。取帧超时允许有限次数重试；连续超过配置阈值后停止，避免程序永久挂起。VSE buffer 校验、注册或 BPU 推理失败属于致命错误，不跳过后继续运行。

`SIGINT` 和 `SIGTERM` handler 只修改原子退出标志。正常与异常退出顺序统一为：

```text
停止取得新帧
  -> 等待当前 BPU 任务结束
  -> 归还所有 VSE frame
  -> 注销 RegisteredBufferCache
  -> 停止并销毁 VFlow/VSE/ISP/VIN/camera
  -> 释放 DNN 输出 tensor 和模型
  -> 关闭 hbmem
```

## 独立工程结构

```text
mipi_vse_bpu_zerocopy/
  CMakeLists.txt
  build.sh
  README.md
  include/
    app_config.hpp
    detection_mapper.hpp
    registered_buffer_cache.hpp
    vio_pipeline.hpp
    vse_frame.hpp
    zero_copy_yolo_runner.hpp
  src/
    app_config.cpp
    detection_mapper.cpp
    main.cpp
    registered_buffer_cache.cpp
    vio_pipeline.cpp
    zero_copy_yolo_runner.cpp
    sensor/
  test/
  scripts/
    run_on_board.sh
```

工程拥有独立 `CMakeLists.txt` 和 `build.sh`，只依赖仓库上级 `3rdlibrary/DNN` 与 `3rdlibrary/RDK_CAMERA` 中的 SDK 头文件和库。它不通过 `model_infer` 分配或预处理输入，避免重新引入复制路径。必要的 YOLO11 输出解析在本工程内实现。

## 命令行和默认行为

板端默认运行方式为：

```bash
./mipi_vse_bpu_zerocopy \
  --model /root/mipi_proj/model/model.bin \
  --frames 100
```

默认配置固定使用 SC132GS、host 0/2、30 FPS、中心裁剪和 640 输出。命令行至少支持模型路径、每路目标帧数、取帧超时、连续超时阈值、score threshold、NMS threshold 和 BPU task timeout。`--frames 0` 表示持续运行直到收到退出信号。

## 测试策略

开发遵循测试先行。纯逻辑与 SDK 调用包装分离，使本地测试无需相机硬件即可覆盖：

- 参数默认值、合法覆盖和非法输入。
- ROI 数值与检测框坐标恢复。
- VSE buffer 尺寸、stride、plane 长度和物理连续性校验。
- 注册缓存首次注册、重复命中、描述符变化拒绝和全部注销。
- VSE 帧在正常、提前返回和异常路径中只归还一次。
- 模型输入属性检查与 tensor 描述符绑定。

硬件集成测试分三层：

1. 加载模型并确认名称、输入来源、类型、形状和字节数。
2. 启动双 VFlow，抓取少量帧并确认 VSE 输出布局满足零拷贝约束。
3. 双路各完成 100 次推理，检查地址相等、缓存注册次数、错误计数和最终汇总。

## 验收标准

- `mipi_vse_bpu_zerocopy/build.sh` 可独立交叉编译出板端程序和测试目标。
- 左右两路各成功完成 100 次 BPU 推理。
- 运行中没有 VSE get/release、内存注册、DNN infer/wait 错误。
- 每次推理的 VSE Y-plane 与 DNN 输入物理地址完全相同。
- 注册次数等于实际出现的不同 VSE pool 地址数，而非帧数。
- 输入路径不调用图像复制或 CPU resize，并且不存在复制兜底。
- 检测框输出为 `1088x1280` 原图坐标。
- README 说明构建、部署、运行、零拷贝证据、错误诊断和资源释放规则。
