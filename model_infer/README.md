# model_infer 模块说明

`model_infer` 负责 RDK 端侧推理链路的实现，包括模型加载、输入预处理、RDK DNN 调用、检测结果解析以及可视化输出。

## 模块职责

本目录主要包含三部分：

- `model_inference_base/`：RDK DNN Runtime 的通用封装
- `yolo_infer/`：YOLO 检测模型的预处理与后处理实现
- `test/`：端到端测试程序，验证模型加载与推理结果

## 目录结构

```text
model_infer/
|-- CMakeLists.txt
|-- build.sh
|-- model_inference_base/
|   |-- model_inference_base.hpp
|   |-- model_inference_base.cpp
|   `-- README.md
|-- yolo_infer/
|   |-- yolo_infer.hpp
|   `-- yolo_infer.cpp
|-- test/
|   `-- test_yolo_infer.cpp
`-- asset/
    |-- 000000440171.jpg
    `-- result.jpg
```

## 模块说明

### model_inference_base

`ModelInferenceBase` 是推理侧的基础封装，负责：

- 加载 `*.bin` 模型
- 查询输入输出 tensor 属性
- 分配与释放 RDK tensor 内存
- 提交推理任务并等待完成
- 统一错误信息管理

该模块适合作为其他模型封装的公共基类。详细说明见 [model_inference_base/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/model_inference_base/README.md)。

### yolo_infer

[yolo_infer/yolo_infer.hpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/yolo_infer/yolo_infer.hpp) 与对应实现文件封装了 YOLO 检测模型的完整推理逻辑，主要包括：

- BGR 图像到模型输入格式的转换
- `resize` / `letterbox` 预处理
- NV12 输入适配
- 多输出头解析
- NMS 后处理
- 结果框恢复到原图坐标系

对外核心接口是：

- `initialize()`
- `infer(const cv::Mat& image)`
- `infer(const YoloInfer::Nv12Frame& frame)`
- `detections()`
- `dumpModelInfo()`

### test

[test/test_yolo_infer.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/test/test_yolo_infer.cpp) 是当前推荐的验证入口，负责：

- 解析模型路径与图片路径
- 初始化 `rdk::YoloInfer`
- 执行推理
- 打印检测结果
- 将检测框绘制到输出图片

## 编译方式

### 依赖

- `aarch64-linux-gnu-gcc`
- `aarch64-linux-gnu-g++`
- `3rdlibrary/DNN`
- `3rdlibrary/opencv_aarch64`
- `cmake` 或 `cmake3`

### 构建命令

在仓库根目录执行：

```bash
cd model_infer
./build.sh
```

### 可覆盖环境变量

- `DNN_ROOT`：RDK DNN SDK 根目录
- `OpenCV_DIR`：OpenCV CMake 包目录
- `BUILD_DIR`：构建输出目录
- `BUILD_TYPE`：构建类型

例如：

```bash
cd model_infer
OpenCV_DIR=/project/3rdlibrary/opencv_aarch64/lib/cmake/opencv4 ./build.sh
```

## 构建产物

默认生成以下内容：

- `build_aarch64/libmodel_inference_base.a`
- `build_aarch64/libyolo_infer.a`
- `build_aarch64/test_yolo_infer`

其中：

- 静态库适合嵌入你自己的端侧工程
- `test_yolo_infer` 适合作为板端功能验证程序

## 板端运行

将以下文件部署到板端：

- `test_yolo_infer`
- `*.bin` 模型文件
- 测试图片
- `libdnn.so`
- OpenCV 相关共享库

典型运行方式如下：

```bash
./test_yolo_infer \
  --model your_model.bin \
  --image your_image.jpg \
  --nv12 \
  --output result.jpg \
  --score 0.25 \
  --nms 0.45 \
  --classes 80
```

程序运行后会：

1. 输出模型输入输出信息
2. 执行一次图像推理
3. 打印检测框结果
4. 生成可视化图片

## 二次集成方式

如果你希望在自己的板端程序中复用当前推理逻辑，可以直接链接 `libmodel_inference_base.a` 和 `libyolo_infer.a`，并以 `rdk::YoloInfer` 作为推理入口。

最小示例如下：

```cpp
#include <opencv2/imgcodecs.hpp>
#include "yolo_infer.hpp"

rdk::ModelInferenceBase::Config config;
config.model_file = "your_model.bin";
config.timeout_ms = 5000;

rdk::YoloInfer::Options options;
options.num_classes = 80;
options.score_threshold = 0.25F;
options.nms_threshold = 0.45F;

rdk::YoloInfer infer(config, options);
if (!infer.initialize()) {
  return -1;
}

cv::Mat image = cv::imread("test.jpg");
if (!infer.infer(image)) {
  return -1;
}

const auto& detections = infer.detections();
```

如果你的输入已经是 NV12，可以直接走 NV12 原生输入路径，不需要先转成 BGR。`crop + resize` 会在预处理阶段完成。

```cpp
#include "yolo_infer.hpp"

rdk::YoloInfer::Nv12Frame frame;
frame.data = nv12_buffer;
frame.width = 2592;
frame.height = 1944;
frame.stride_y = 2592;
frame.stride_uv = 2592;
frame.crop_x = 0;
frame.crop_y = 0;
frame.crop_width = 2592;
frame.crop_height = 1944;

if (!infer.infer(frame)) {
  return -1;
}
```

## 结果展示

### 输入图像

![输入图像](asset/000000440171.jpg)

### 输出结果

![输出结果](asset/result.jpg)

## 注意事项

- 当前构建面向 `aarch64`，不能直接在 x86 主机上运行生成的可执行程序
- `*.bin` 模型必须与当前推理逻辑的输入约定一致
- NV12 输入要求宽高、crop 坐标和 crop 尺寸为偶数
- 若更换模型输出头结构，需要同步调整 `yolo_infer` 的后处理逻辑
- 若板端缺少运行时依赖，需要正确设置 `LD_LIBRARY_PATH`
