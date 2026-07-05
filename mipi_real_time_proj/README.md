# mipi_real_time_proj

`mipi_real_time_proj` 是一个面向 RDK X5 的独立 MIPI 摄像头实时推理工程。工程基于本地导入的 `hobot_mipi_cam` 代码采集 GS130W 双目摄像头 NV12 数据，并调用同级目录 `../model_infer` 中的 YOLO 推理接口完成 BPU 推理、结果输出、链路耗时记录和可视化保存。

本工程的目标是把“摄像头采集 -> NV12 帧封装 -> 模型推理 -> 结果记录/可视化”整理成一个可独立编译、可上板运行、便于定位性能瓶颈的最小闭环。

## 目录结构

```text
mipi_real_time_proj/
  CMakeLists.txt                         独立 CMake 工程入口
  build.sh                               aarch64 交叉编译脚本
  README.md                              本文档

  include/
    mipi_zero_copy_app.hpp               应用配置、参数解析、帧视图转换声明
    hobot_mipi_*.hpp                     从 hobot_mipi_cam 导入/适配的摄像头接口
    rclcpp/, sensor_msgs/                独立工程使用的轻量兼容头
    json/, yaml-cpp/                     构建期兼容头

  src/
    mipi_zero_copy_app.cpp               默认配置、命令行解析、路径归一化、CapInfo 构造
    mipi_zero_copy_infer.cpp             主程序：采集、推理、日志、可视化
    hobot_mipi_*.cpp                     hobot_mipi_cam 相关实现
    camera.cpp                           保留的 camera 适配代码
    sensor/                              sensor 配置源文件

  config/
    SC132gs_dual_calibration.yaml        GS130W 双目默认标定文件
    *.yaml                               其他 sensor/双目配置示例

  scripts/
    run_mipi_zero_copy_test.sh           上板测试脚本，自动保存日志和可视化

  test/
    test_mipi_zero_copy_config.cpp       配置、参数解析、NV12 帧视图单元测试

  build_aarch64/                         build.sh 生成目录，不建议手工维护
```

外部依赖目录位于工程上一级：

```text
RDK_proj/
  model_infer/                           YOLO/DNN 推理封装，CMake 会 add_subdirectory
  3rdlibrary/
    DNN/                                 RDK DNN SDK 头文件和库
    opencv_aarch64/                      aarch64 OpenCV
    RDK_CAMERA/                          已拷贝的 RDK camera SDK 头文件和库
```

## 默认硬件配置

当前默认配置面向 GS130W 双目摄像头和 board `302`，左右两路 MIPI host 分别为 `0` 和 `2`。

```text
sensor: sc132gs-1280p
device_mode: dual
dual_combine: 0
out_format: nv12
gdc_enable: false
image_width: 1088
image_height: 1280
channel: 0
channel2: 2
infer_channels: left,right
calibration: ~/mipi_proj/config/SC132gs_dual_calibration.yaml
model: ~/mipi_proj/model/model.bin
visualization rotation: 90 degrees clockwise
```

程序会在启动后展开 `--model`、`--config-path`、`--calibration`、`--output-dir` 中开头的 `~`，并把相对路径归一化为绝对路径。因此默认模型路径 `~/mipi_proj/model/model.bin` 在 root 用户下会解析成 `/root/mipi_proj/model/model.bin`。

## 技术架构

核心链路如下：

```text
HobotMipiCap::getFrame(channel)
  -> mipi_cam::VideoBuffer
  -> makeNv12FrameView(VideoBuffer)
  -> rdk::YoloInfer::infer(Nv12Frame)
  -> printDetections()
  -> RunArtifacts 写 JSONL/summary/JPEG
  -> VideoBuffer::return_empty_que()
```

关键点：

- 主程序不调用 `MipiCam::getImage()` 或 `getImageMem()`。
- 推理输入通过 `makeNv12FrameView()` 直接把 `VideoBuffer::buff.data()` 封装成 `rdk::YoloInfer::Nv12Frame`。
- 当前导入的 `hobot_mipi_cam` 实现会在 `getVnodeFrame()` 内部把 HBN VIO 输出拷贝到 `VideoBuffer::buff`。本工程避免了应用层再拷贝到额外图像缓冲区。
- `ReturnedFrame` 使用 RAII 管理帧归还，离开作用域时调用 `return_empty_que()`，避免异常路径或 `continue` 分支遗漏归还。
- 推理、打印、可视化、总帧耗时会按帧记录，便于观察端到端链路。

## 双路摄像头到推理的全过程

本节按真实源码路径说明一帧图像从 GS130W 双目摄像头进入程序，到完成 YOLO 推理、输出检测结果、归还相机缓冲区的全过程。

### 1. 应用启动和配置生成

主入口在 `src/mipi_zero_copy_infer.cpp`。

启动后先执行：

```text
defaultConfig()
  -> parseArgs()
  -> normalizePaths()
```

`defaultConfig()` 位于 `src/mipi_zero_copy_app.cpp`，它给 GS130W 双目设置默认值：

```text
sensor = sc132gs-1280p
device_mode = dual
channel = 0
channel2 = 2
infer_channels = left,right
out_format = nv12
image_width = 1088
image_height = 1280
gdc_enable = false
```

`parseArgs()` 用命令行参数覆盖默认值。例如 `--infer-channel left,right` 会解析成 `std::vector<std::string>{"left", "right"}`；`--dual-combine 2 --infer-channel combine` 则会让后续主循环只取合并后的双目图像。

`normalizePaths()` 会展开开头的 `~`，并把路径转为绝对路径。这个步骤发生在 DNN 模型初始化之前，因此默认模型路径不会以字面量 `~/...` 传给 `hbDNNInitializeFromFiles()`。

随后程序初始化两个独立对象：

```text
rdk::YoloInfer infer(infer_config, yolo_options)
CameraSession camera
RunArtifacts artifacts
```

`YoloInfer::initialize()` 加载 `.bin` 模型、读取输入输出 tensor 属性、分配 DNN 输入/输出 buffer。`CameraSession::open()` 初始化 MIPI camera pipeline。`RunArtifacts::open()` 创建日志、JSONL、summary 和可视化输出目录。

### 2. 双路 MIPI 摄像头初始化

`CameraSession::open()` 的核心流程是：

```text
createMipiCap(getBoardType())
  -> initEnv()
  -> makeCapInfo(config)
  -> getDualCamCalibrationIml(...)
  -> setCamInfo(...)
  -> init(cap_info)
  -> start()
```

`makeCapInfo()` 会把应用层 `AppConfig` 转成 `mipi_cam::MIPI_CAP_INFO_ST`。关键字段如下：

```text
cap_info.sensor_type = "sc132gs-1280p"
cap_info.width = 1088
cap_info.height = 1280
cap_info.fps = 30
cap_info.channel_ = 0
cap_info.channel2_ = 2
cap_info.device_mode_ = "dual"
cap_info.dual_combine_ = 0
cap_info.out_format_name = "nv12"
```

`device_mode_="dual"` 表示创建两路 camera pipeline。`channel_` 和 `channel2_` 分别指定左右两路要绑定的 MIPI host。对 board `302`，通常就是 host `0` 和 host `2`。

标定加载发生在 `cap_->init(cap_info)` 之前。双目模式下，程序调用：

```text
mipi_calibration::GetInstance().getDualCamCalibrationIml(
    cam_info[0], cam_info[1], config.calibration_path)
cap_->setCamInfo(cam_info)
```

这一步把左右相机内参、畸变参数、双目外参等信息加载进 `HobotMipiCap`。当前默认关闭 GDC，因此这些标定主要用于 camera 模块内部保留和后续扩展；推理链路本身直接消费未矫正的 NV12 图像。

### 3. hobot_mipi_cam 内部如何产生 left/right

实际 camera 实现是 `HobotMipiCapIml`，位于 `src/hobot_mipi_cap_iml.cpp`。

初始化时，`HobotMipiCapIml::init()` 会：

```text
检测当前板卡支持的 MIPI host
  -> 找到 channel_ 和 channel2_ 对应的 sensor
  -> pipe_contex.resize(2)
  -> 为每一路配置 sensor、CSI、VIN、ISP、VSE 等 VIO pipeline
```

启动时，`HobotMipiCapIml::start()` 会：

```text
对每个 pipe_contex 调用 hbn_vflow_start()
  -> 为每一路创建 BuffQueueManage
  -> 每路预创建 5 个 VideoBuffer
  -> 启动 multiFrameTask 采集线程
  -> 如 dual_combine 为 1/2，再启动 sync_task 合成线程
```

每一路图像都有一个 `BuffQueueManage`。它内部维护两个队列：

```text
q_empty_  空闲 VideoBuffer 队列
q_buff_   已填充数据、等待应用层取走的 VideoBuffer 队列
```

`VideoBuffer` 是跨 camera 模块和应用层传递的帧对象，关键字段包括：

```text
timestamp       时间戳
frame_id        帧号
width/height    图像尺寸
stride          行跨度
buff_size       Y + UV 总字节数
buff            NV12 数据，先 Y plane 后 UV plane
encode          当前为 "nv12"
```

后台采集线程 `multiFrameTask()` 会对两路 VSE fd 做 `select()`。任一路有新帧可读时，流程如下：

```text
v_buff_que_manger_[i]->get_empty_buff()
  -> getVnodeFrame(pipe_contex[i].stream_handle, 0, buff_ptr)
  -> buff_ptr->return_data_que()
```

`i=0` 对应 `left`，`i=1` 对应 `right`。因此应用层调用：

```text
getFrame("left")  -> v_buff_que_manger_[0]->get_data_buff()
getFrame("right") -> v_buff_que_manger_[1]->get_data_buff()
```

`getFrame()` 本身不会主动从硬件抓帧，它只是从对应数据队列取出后台线程已经填好的 `VideoBuffer`。如果队列暂时为空，它会按 `fps` 推导的等待次数短暂 sleep 重试，最终仍为空则返回 `nullptr`。

### 4. 从 HBN VIO 输出到 VideoBuffer

`getVnodeFrame()` 是 HBN/VIO 图像进入本工程 `VideoBuffer` 的边界：

```text
hbn_vnode_getframe(handle, channel, 1000, &out_img)
  -> hb_mem_invalidate_buf_with_vaddr(Y)
  -> hb_mem_invalidate_buf_with_vaddr(UV)
  -> 填写 frame_id/timestamp/width/height/stride
  -> buff.resize(Y_size + UV_size)
  -> memcpy(Y plane  到 buff.data())
  -> memcpy(UV plane 到 buff.data() + Y_size)
  -> hbn_vnode_releaseframe(handle, channel, &out_img)
```

这里需要准确区分“零拷贝”的边界：

- HBN VIO 输出到 `VideoBuffer::buff`：当前导入的 `hobot_mipi_cam` 代码会执行一次 `memcpy`，把 HBN 输出的 Y/UV plane 拷贝进 `std::vector<char> buff`。
- `VideoBuffer::buff` 到推理输入视图：本工程不再复制图像数据，只创建 `Nv12Frame` 指针视图。
- `Nv12Frame` 到 DNN 输入 tensor：`model_infer` 的预处理会根据模型输入尺寸、stride、crop/letterbox 情况，把 NV12 写入 DNN 输入 tensor buffer。这是模型执行前必要的数据准备。
- 保存 JPEG 可视化：只有开启 `--save-vis` 时才额外做 `NV12 -> BGR -> rotate -> draw -> imwrite`，这条路径不参与模型推理输入。

因此，本工程所谓“零拷贝摄像头数据流”指的是应用层从 `VideoBuffer` 到 `YoloInfer::infer()` 不再引入额外图像 Mat 或中间图像缓冲；它不表示整个 HBN 驱动到 BPU tensor 的全链路完全无拷贝。

### 5. 主循环如何处理双路图像

主循环位于 `mipi_zero_copy_infer.cpp`：

```text
while running and frame_index < frame_count:
  for channel in infer_channels:
    frame = camera.getFrame(channel)
    nv12 = makeNv12FrameView(frame.get())
    infer.infer(nv12)
    printDetections(...)
    saveVisualization(...)   可选
    writeFrameRecord(...)
  frame_index++
```

默认 `infer_channels={"left","right"}`，所以一个 `frame_index` 内会依次处理左目和右目：

```text
frame_index = 0:
  left  -> getFrame("left")  -> infer -> log/vis -> return buffer
  right -> getFrame("right") -> infer -> log/vis -> return buffer

frame_index = 1:
  left  -> ...
  right -> ...
```

当前实现是单线程顺序推理，不是左右两路并行推理。好处是链路简单、日志容易对齐、BPU 调用不会被多线程同时竞争；代价是端到端吞吐由“左目推理 + 右目推理 + 可选可视化”的总时间决定。

`CameraSession::getFrame()` 返回的是 `ReturnedFrame` 包装对象。它持有 `std::shared_ptr<VideoBuffer>`，析构时自动执行：

```text
frame_->return_empty_que()
```

这意味着主循环每次取出的相机缓冲区在当前通道处理结束后会回到 `q_empty_`，供后台采集线程重新填充。即使某一帧推理失败、保存可视化失败或提前 `continue`，只要 `ReturnedFrame` 离开作用域，buffer 都会被归还。

### 6. NV12 数据如何传给 YOLO 推理

`makeNv12FrameView()` 是应用层数据传递的关键函数。它不分配图像内存，不做颜色转换，只根据 `VideoBuffer` 字段构造一个轻量视图：

```text
data      = reinterpret_cast<const uint8_t*>(buffer.buff.data())
y_plane   = data
uv_plane  = data + stride * height
width     = buffer.width
height    = buffer.height
stride_y  = buffer.stride
stride_uv = buffer.stride
crop      = full image
source    = full image
preprocessed = false
```

NV12 在内存中按如下方式排列：

```text
buffer.buff
  +-------------------------------+
  | Y plane: stride * height      |
  +-------------------------------+
  | UV plane: stride * height / 2 |
  +-------------------------------+

y_plane  = buff.data()
uv_plane = buff.data() + stride * height
```

随后主程序调用：

```cpp
infer.infer(nv12);
```

`YoloInfer::infer(const Nv12Frame&)` 会把该视图包装为 `InferInputView`，再进入 `ModelInferenceBase::infer()`。因此主程序和 `model_infer` 之间传递的是指向原始 NV12 buffer 的描述结构，而不是 OpenCV `cv::Mat` 图像副本。

### 7. model_infer 内部推理流程

`model_infer` 的公共入口是 `rdk::YoloInfer`，文件位于 `../model_infer/yolo_infer/`。

调用 `infer.infer(nv12)` 后，内部流程是：

```text
YoloInfer::infer(Nv12Frame)
  -> ModelInferenceBase::infer(input_view)
     -> YoloInfer::preprocess(...)
     -> hbDNNInfer(...)
     -> hbDNNWaitTaskDone(...)
     -> YoloInfer::postprocess(...)
     -> detections_ 更新
```

`YoloInfer::preprocess()` 会检查模型输入 tensor 是否为 NV12 或 NV12 separate 类型，检查输入宽高、stride、crop 是否合法。当前 `makeNv12FrameView()` 设置 `preprocessed=false`，所以预处理会根据模型输入尺寸执行必要的 crop、resize、letterbox 和 NV12 tensor 写入。模型示例是 `640x640` NV12 输入时，原始 `1088x1280` NV12 会在这里进入模型需要的输入布局。

`ModelInferenceBase::infer()` 调用 DNN runtime：

```text
hbDNNInfer()
hbDNNWaitTaskDone(timeout_ms)
```

等待 BPU 任务完成后，`YoloInfer::postprocess()` 解析模型输出 tensor，执行 YOLO head 解码、score threshold、NMS，并把最终检测框写入：

```text
std::vector<YoloInfer::Detection> detections_
```

主程序通过：

```cpp
infer.detections()
```

读取结果用于打印、JSONL 记录和可视化画框。

### 8. 日志、JSONL 和可视化如何接入

每个通道每帧都会记录一组耗时：

```text
get_frame_ms
wrap_nv12_ms
infer_ms
print_result_ms
save_visualization_ms
total_frame_ms
```

这些耗时覆盖了应用层主链路：

```text
getFrame()
  -> makeNv12FrameView()
  -> YoloInfer::infer()
  -> printDetections()
  -> optional saveVisualization()
  -> writeFrameRecord()
```

如果开启 `--log-jsonl`，`RunArtifacts::writeFrameRecord()` 会把每帧信息写入 `frames.jsonl`，包括通道、分辨率、stride、检测框列表和耗时。`summary.txt` 则按通道统计 warmup 后的均值和估算 FPS。

如果开启 `--save-vis`，可视化流程才会触发：

```text
Nv12Frame
  -> cv::Mat y/uv 指向 y_plane/uv_plane
  -> cv::cvtColorTwoPlane(..., COLOR_YUV2BGR_NV12)
  -> rotateImageClockwise(...)
  -> rotateRectClockwise(...)
  -> cv::rectangle / cv::putText
  -> cv::imwrite(...)
```

注意：可视化使用 OpenCV 转 BGR 并保存 JPEG，只用于观察结果，不会回写到推理输入，也不会影响 BPU 推理结果。默认 `--vis-rotate 90` 是为当前 GS130W 安装方向设置的保存结果方向修正。

### 9. 双目 combine 模式的数据流

默认 `dual_combine=0`，只输出左右两路：

```text
left  -> v_buff_que_manger_[0]
right -> v_buff_que_manger_[1]
```

如果设置 `dual_combine=1` 或 `dual_combine=2`，`HobotMipiCapIml::start()` 会启用 `combine_flag_` 并启动 `sync_task()`。后台采集线程在获取左右帧后，会额外把帧副本推入 `v_frame_que_`，同步线程按 timestamp 做对齐：

```text
left frame queue  \
                   -> sync_task() -> combine_buff_que_manger_ -> getFrame("combine")
right frame queue /
```

`sync_task()` 找到时间戳在容差内的左右帧后，会构造一个新的 `VideoBuffer`。NV12 combine 的内存布局是先拼接所有 Y plane，再拼接所有 UV plane：

```text
combined buff
  +---------------------+
  | left  Y             |
  | right Y             |
  +---------------------+
  | left  UV            |
  | right UV            |
  +---------------------+
```

同时它会设置：

```text
width  = single_width
height = single_height * frame_count
stride = single_stride
```

因此 `combine` 更像是竖向拼接后的单张 NV12 图。使用方式：

```bash
./mipi_zero_copy_infer --dual-combine 2 --infer-channel combine --frames 100
```

当前默认推理模型通常按单目画面训练和输入。如果要对 `combine` 做检测，需要确认模型输入、目标尺度、后处理坐标解释是否符合拼接图语义。

### 10. 端到端数据生命周期

默认左右双路推理的数据生命周期如下：

```text
硬件/驱动侧
  GS130W left sensor  -> MIPI host 0 -> VIN/ISP/VSE -> hbn_vnode_image_t
  GS130W right sensor -> MIPI host 2 -> VIN/ISP/VSE -> hbn_vnode_image_t

hobot_mipi_cam 后台线程
  hbn_vnode_getframe()
    -> invalidate cache
    -> memcpy Y/UV 到 VideoBuffer::buff
    -> hbn_vnode_releaseframe()
    -> VideoBuffer 进入 q_buff_

应用主线程
  getFrame("left"/"right")
    -> 从 q_buff_ 取出 shared_ptr<VideoBuffer>
    -> makeNv12FrameView() 创建 Nv12Frame 指针视图
    -> YoloInfer::infer(Nv12Frame)
    -> detections() 读取检测结果
    -> 可选保存可视化
    -> 写日志/JSONL/summary
    -> ReturnedFrame 析构
    -> VideoBuffer 回到 q_empty_
```

从所有权角度看：

```text
BuffQueueManage owns reusable VideoBuffer objects
  -> get_data_buff() 临时转交给应用层 shared_ptr
  -> ReturnedFrame 持有 shared_ptr 保证处理期间 buffer 有效
  -> return_empty_que() 归还给 BuffQueueManage
```

从指针有效期看：

- `Nv12Frame::data/y_plane/uv_plane` 指向 `VideoBuffer::buff.data()`。
- `Nv12Frame` 只在当前通道当前帧处理期间有效。
- 不要把 `Nv12Frame` 或其 plane 指针保存到异步线程中使用。
- 如果后续改成异步推理，必须让 `VideoBuffer` 的生命周期覆盖整个 BPU 输入准备和执行阶段。

### 11. 一帧处理的时序图

默认 `left,right` 顺序推理时，一次 `frame_index` 的时序可以理解为：

```text
后台采集线程 multiFrameTask
  [left ready]  getVnodeFrame -> q_buff_[left]
  [right ready] getVnodeFrame -> q_buff_[right]

主线程 frame_index=N
  getFrame("left")
    -> makeNv12FrameView
    -> YoloInfer::preprocess
    -> hbDNNInfer / hbDNNWaitTaskDone
    -> YoloInfer::postprocess
    -> print/save/log
    -> return_empty_que(left buffer)

  getFrame("right")
    -> makeNv12FrameView
    -> YoloInfer::preprocess
    -> hbDNNInfer / hbDNNWaitTaskDone
    -> YoloInfer::postprocess
    -> print/save/log
    -> return_empty_que(right buffer)

  frame_index++
```

这也解释了日志中 `frame=0 channel=left` 和 `frame=0 channel=right` 的关系：它们是应用层同一个循环索引下的左右两路处理记录，不保证二者来自硬件严格同一曝光时刻。若业务要求严格同步，应使用 `dual_combine` 或在应用层增加 timestamp 对齐策略。

## 构建

### 在 Docker 容器中编译

工程已在容器 `3c543521b4ca` 中验证过，宿主目录在容器内挂载为 `/project`。

```bash
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_real_time_proj && ./build.sh'
```

默认输出：

```text
mipi_real_time_proj/build_aarch64/mipi_zero_copy_infer
```

### 只编译配置测试

该目标不需要完整 camera 硬件 SDK，可用于快速检查参数解析和默认配置是否可编译。

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_real_time_proj && ./build.sh --target test_mipi_zero_copy_config'
```

注意：该工程默认交叉编译 aarch64 产物。如果当前 Docker 容器不是 aarch64 runtime 环境，直接执行测试二进制可能出现：

```text
bash: ./test_mipi_zero_copy_config: No such file or directory
```

这通常不是文件不存在，而是缺少 aarch64 动态加载器或 qemu 运行环境。此容器主要用于交叉编译验证，真正运行应在 RDK 板端完成。

### 构建脚本参数

`build.sh` 支持通过环境变量覆盖默认路径：

```bash
BUILD_DIR=/tmp/mipi_build \
BUILD_TYPE=Debug \
DNN_ROOT=/project/3rdlibrary/DNN \
OpenCV_DIR=/project/3rdlibrary/opencv_aarch64/lib/cmake/opencv4 \
RDK_CAMERA_ROOT=/project/3rdlibrary/RDK_CAMERA \
./build.sh
```

如果 SDK 不在默认位置，可以显式传入 include/lib：

```bash
RDK_CAMERA_INCLUDE_DIRS="/usr/include;/opt/hobot/include" \
RDK_CAMERA_LIBRARY_DIRS="/usr/lib/aarch64-linux-gnu;/opt/hobot/lib" \
RDK_CAMERA_LIBRARIES="vpf;cam;hbmem;gdcbin;Nano2D;Nano2Dutil;pthread" \
./build.sh
```

`CMakeLists.txt` 会检查关键头文件，例如 `hbn_api.h`、`hb_camera_interface.h`、`vin_cfg.h`、`hb_media_codec.h`。如果配置阶段报缺少这些头文件，说明 RDK camera SDK 头文件没有被正确放到 `3rdlibrary/RDK_CAMERA/include` 或没有通过 `RDK_CAMERA_INCLUDE_DIRS` 指定。

## 部署到 RDK 板端

推荐板端目录结构：

```text
~/mipi_proj/
  mipi_zero_copy_infer
  model/
    model.bin
  config/
    SC132gs_dual_calibration.yaml
```

如果你直接在 `/project/mipi_real_time_proj/build_aarch64` 运行，也可以使用绝对路径指定模型和配置。

运行前设置库路径：

```bash
export LD_LIBRARY_PATH=/project/3rdlibrary/RDK_CAMERA/lib:/project/3rdlibrary/DNN/lib:/project/3rdlibrary/opencv_aarch64/lib:$LD_LIBRARY_PATH
```

最小运行命令：

```bash
cd /project/mipi_real_time_proj/build_aarch64
./mipi_zero_copy_infer --frames 100
```

显式指定模型：

```bash
./mipi_zero_copy_infer \
  --model ~/mipi_proj/model/model.bin \
  --frames 100
```

## 使用测试脚本

推荐用脚本运行，因为脚本会自动创建输出目录、保存完整 stdout/stderr、保存 JSONL 和可视化结果。

```bash
cd /project/mipi_real_time_proj
./scripts/run_mipi_zero_copy_test.sh
```

默认输出目录：

```text
runs/mipi_zero_copy_YYYYmmdd_HHMMSS/
  run.log                              完整运行日志
  frames.jsonl                         每帧结构化耗时和检测结果
  summary.txt                          warmup 后的统计汇总
  vis/
    frame_000000_left.jpg              左目可视化图
    frame_000000_right.jpg             右目可视化图
```

常用脚本参数：

```bash
# 跑 300 帧，每 10 帧保存一次可视化
FRAMES=300 SAVE_EVERY=10 ./scripts/run_mipi_zero_copy_test.sh

# 不保存 JPEG，只记录日志和 JSONL
SAVE_VIS=0 FRAMES=1000 ./scripts/run_mipi_zero_copy_test.sh

# 指定输出目录和阈值
OUTPUT_DIR=/tmp/mipi_test ./scripts/run_mipi_zero_copy_test.sh --score 0.35

# 修改可视化方向。GS130W 当前默认顺时针 90 度
VIS_ROTATE=0 ./scripts/run_mipi_zero_copy_test.sh
VIS_ROTATE=90 ./scripts/run_mipi_zero_copy_test.sh
```

脚本支持的主要环境变量：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `EXE` | `build_aarch64/mipi_zero_copy_infer` | 可执行文件路径 |
| `MODEL` | `../model_convert/...nv12.bin` | 脚本运行时传入的模型路径 |
| `RUN_ROOT` | `runs` | 输出根目录 |
| `OUTPUT_DIR` | 自动时间戳目录 | 单次运行输出目录 |
| `SENSOR` | `sc132gs-1280p` | sensor 名称 |
| `WIDTH` | `1088` | 采集宽度 |
| `HEIGHT` | `1280` | 采集高度 |
| `CHANNEL` | `0` | 第一 MIPI host |
| `CHANNEL2` | `2` | 第二 MIPI host |
| `INFER_CHANNEL` | `left,right` | 推理通道列表 |
| `FRAMES` | `100` | 每个通道处理帧数，`0` 表示持续运行 |
| `WARMUP` | `2` | 统计汇总时跳过前 N 帧 |
| `SAVE_VIS` | `1` | 是否保存 JPEG 可视化 |
| `SAVE_EVERY` | `5` | 每 N 帧保存一次 JPEG |
| `VIS_ROTATE` | `90` | JPEG 顺时针旋转角度 |

## 命令行参数

主程序支持以下参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--model PATH` | `~/mipi_proj/model/model.bin` | RDK `.bin` 模型路径 |
| `--sensor NAME` | `sc132gs-1280p` | sensor 配置名称 |
| `--width N` | `1088` | 采集宽度 |
| `--height N` | `1280` | 采集高度 |
| `--fps N` | `30` | 采集帧率 |
| `--channel N` | `0` | 第一 MIPI host |
| `--channel2 N` | `2` | 第二 MIPI host |
| `--infer-channel LIST` | `left,right` | 逗号分隔的 `getFrame()` 通道 |
| `--frames N` | `0` | 每个通道处理帧数，`0` 表示持续运行 |
| `--score F` | `0.25` | 检测置信度阈值 |
| `--nms F` | `0.45` | NMS 阈值 |
| `--classes N` | `80` | 类别数 |
| `--bpu-core N` | runtime 自动选择 | 指定 BPU core id |
| `--timeout-ms N` | `5000` | DNN 推理超时 |
| `--config-path PATH` | `~/mipi_proj/config` | camera 配置目录 |
| `--calibration PATH` | `~/mipi_proj/config/SC132gs_dual_calibration.yaml` | 标定文件 |
| `--output-dir PATH` | 空 | 保存 JSONL、summary、JPEG 的目录 |
| `--save-vis` | 关闭 | 保存 JPEG 可视化 |
| `--save-every N` | `1` | 每 N 帧保存一次 JPEG |
| `--vis-rotate N` | `90` | JPEG 顺时针旋转角度，仅支持 `0/90/180/270` |
| `--log-jsonl` | 关闭 | 写入 `frames.jsonl` |
| `--warmup N` | `0` | 汇总统计时跳过前 N 帧 |
| `--dual-combine N` | `0` | `hobot_mipi_cam` 双目 combine 模式 |
| `--no-gdc` | 默认已关闭 | 禁用 GDC |
| `--help` | - | 打印帮助 |

示例：

```bash
./mipi_zero_copy_infer \
  --model ~/mipi_proj/model/model.bin \
  --sensor sc132gs-1280p \
  --width 1088 \
  --height 1280 \
  --channel 0 \
  --channel2 2 \
  --infer-channel left,right \
  --frames 100 \
  --output-dir ~/mipi_proj/runs/manual \
  --log-jsonl \
  --save-vis \
  --save-every 5 \
  --vis-rotate 90 \
  --warmup 2 \
  --score 0.25 \
  --nms 0.45
```

如果使用合并双目输出：

```bash
./mipi_zero_copy_infer \
  --dual-combine 2 \
  --infer-channel combine \
  --frames 100
```

## 日志和性能分析

程序会输出两类核心日志。

第一类是推理结果和模型信息，例如：

```text
model_name=yolo11m_bayese_640x640_nv12
input[0] valid=[1, 3, 640, 640], ...
[result] frame=0 channel=left detections=3
```

第二类是链路耗时：

```text
[perf] [mipi_zero_copy_chain] frame=0 channel=left counted=0 \
  get_frame_ms=... wrap_nv12_ms=... infer_ms=... \
  print_result_ms=... save_visualization_ms=... total_frame_ms=...
```

各字段含义：

| 字段 | 含义 |
| --- | --- |
| `get_frame_ms` | 从 `HobotMipiCap::getFrame()` 获取一帧的耗时 |
| `wrap_nv12_ms` | 把 `VideoBuffer` 封装为 `Nv12Frame` 视图的耗时 |
| `infer_ms` | `YoloInfer::infer()` 总耗时，包括模型接口内部处理 |
| `print_result_ms` | 打印检测框结果耗时 |
| `save_visualization_ms` | NV12 转 BGR、旋转、画框、写 JPEG 的耗时 |
| `total_frame_ms` | 单个通道单帧端到端耗时 |
| `counted` | 是否计入 summary。前 `warmup` 帧不计入 |

`frames.jsonl` 每行对应一个 frame/channel，适合后续用 Python、jq 或 pandas 做分析。`summary.txt` 会按通道统计平均耗时、最大帧耗时和估算 FPS。

## 可视化输出

保存 JPEG 时，程序会执行：

```text
NV12 two-plane -> BGR -> clockwise rotate -> transform boxes -> draw boxes -> imwrite
```

GS130W 当前成像方向默认需要顺时针旋转 90 度，因此默认 `--vis-rotate 90`。如果后续换 sensor 或镜头安装方向变化，可以改为：

```bash
--vis-rotate 0
--vis-rotate 180
--vis-rotate 270
```

检测框会跟随旋转后的图像同步变换，不需要额外后处理。

## 关键源码说明

### `mipi_zero_copy_app.cpp`

负责应用层配置：

- `defaultConfig()`：默认 GS130W 双目参数。
- `parseArgs()`：解析命令行参数并做基本合法性检查。
- `normalizePaths()`：展开 `~`，把路径转成绝对路径。
- `makeCapInfo()`：把 `AppConfig` 转成 `mipi_cam::MIPI_CAP_INFO_ST`。
- `makeNv12FrameView()`：把 `VideoBuffer` 封装成 `rdk::YoloInfer::Nv12Frame`。

### `mipi_zero_copy_infer.cpp`

负责主流程：

- 初始化 DNN/YOLO 模型。
- 初始化 `HobotMipiCap` 和双目标定。
- 循环读取 `left/right/combine` 帧。
- 对每帧调用 `YoloInfer::infer()`。
- 打印检测结果和每步耗时。
- 按需保存 JSONL、summary 和 JPEG。
- 捕获 `SIGINT/SIGTERM` 后退出循环，释放 camera 和 DNN 资源。

### `scripts/run_mipi_zero_copy_test.sh`

负责测试运行封装：

- 创建 timestamp 输出目录。
- 设置 `LD_LIBRARY_PATH`。
- 组装默认参数。
- 把完整 stdout/stderr 通过 `tee` 保存到 `run.log`。
- 支持用环境变量快速覆盖模型、帧数、可视化频率、旋转角度等参数。

## 常见问题

### DNN 报模型文件打不开

典型日志：

```text
Can not open ~/mipi_proj/model/model.bin, file not exists or no read permission.
```

当前版本已经在程序内展开 `~`。如果仍报错，检查：

```bash
ls -l ~/mipi_proj/model/model.bin
```

如果模型放在其他目录，直接传绝对路径：

```bash
./mipi_zero_copy_infer --model /path/to/model.bin --frames 100
```

### 编译时报缺少 `hbn_api.h`

说明 camera SDK 头文件没有进入 CMake 搜索路径。检查：

```bash
ls /project/3rdlibrary/RDK_CAMERA/include/hbn_api.h
```

或者显式传入：

```bash
RDK_CAMERA_INCLUDE_DIRS="/path/to/sdk/include;/opt/hobot/include" ./build.sh
```

### 可视化图片方向不对

使用 `--vis-rotate` 或脚本环境变量 `VIS_ROTATE`：

```bash
./mipi_zero_copy_infer --save-vis --vis-rotate 0 --frames 20
VIS_ROTATE=270 ./scripts/run_mipi_zero_copy_test.sh
```

### `getFrame` 返回空

优先检查：

- 板卡 `board_id` 是否对应 `channel=0/channel2=2`。
- 摄像头排线和供电是否正常。
- sensor 名称是否为当前硬件支持的配置。
- 是否已有其他进程占用 camera/VIO。
- `LD_LIBRARY_PATH` 是否包含 camera SDK 运行库。

可在板端查看：

```bash
cat /sys/class/socinfo/board_id
cat /etc/board_config.json
ls /dev/i2c-*
```

### Docker 内不能执行测试二进制

`build.sh` 生成的是 aarch64 产物。x86 Docker 容器没有 aarch64 loader/qemu 时，执行会失败。此时以交叉编译通过为准，上板后再运行程序。

## 开发建议

- 修改默认 camera/model 参数时，同步更新 `defaultConfig()` 和 `test/test_mipi_zero_copy_config.cpp`。
- 新增命令行参数时，同步更新 `usage()`、README 参数表和配置测试。
- 修改可视化或日志字段时，同步检查 `run_mipi_zero_copy_test.sh` 和 `summary.txt`/`frames.jsonl` 的消费方式。
- 不要在主链路中额外复制 NV12 数据，除非明确需要做格式转换或离线保存。

## 快速命令清单

```bash
# 编译完整工程
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_real_time_proj && ./build.sh'

# 只编译配置测试
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_real_time_proj && ./build.sh --target test_mipi_zero_copy_config'

# 板端运行 100 帧
cd /project/mipi_real_time_proj/build_aarch64
export LD_LIBRARY_PATH=/project/3rdlibrary/RDK_CAMERA/lib:/project/3rdlibrary/DNN/lib:/project/3rdlibrary/opencv_aarch64/lib:$LD_LIBRARY_PATH
./mipi_zero_copy_infer --frames 100

# 板端运行并保存日志/可视化
cd /project/mipi_real_time_proj
FRAMES=100 SAVE_EVERY=5 VIS_ROTATE=90 ./scripts/run_mipi_zero_copy_test.sh
```
