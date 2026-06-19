# RDK 模型转换与推理工程

本仓库面向地瓜机器人 RDK 平台，提供一套从训练模型导出、板端模型转换到端侧推理验证的工程化样例。当前主线聚焦于 YOLO 类检测模型，覆盖 `PT -> ONNX -> BIN -> RDK 端侧推理` 的完整流程。

## 项目概览

仓库主要分为两条工作链路：

- **模型转换**：将训练得到的 `*.pt` 模型导出为 `*.onnx`，再进一步转换为 RDK 侧可部署的 `*.bin`
- **模型推理**：基于 RDK DNN Runtime 与 OpenCV，完成模型加载、预处理、推理执行、后处理与结果可视化

## 整体流程

```text
Ultralytics *.pt
    -> model_convert/pt2onnx
ONNX 浮点模型
    -> model_convert/onnx2bin
RDK *.bin 模型
    -> model_infer
端侧检测结果 / 可视化输出
```

## 目录结构

```text
.
|-- model_convert/          # 模型转换链路
|   |-- pt2onnx/            # PT 导出 ONNX
|   `-- onnx2bin/           # ONNX 转 RDK BIN
|-- model_infer/            # 端侧推理链路
|   |-- model_inference_base/
|   |-- yolo_infer/
|   |-- test/
|   `-- asset/
|-- 3rdlibrary/             # DNN/OpenCV 等第三方依赖
`-- perf/                   # 板端执行与日志辅助脚本
```

## 快速开始

### 1. 模型转换

模型转换入口位于 [model_convert/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_convert/README.md)。

典型流程为：

1. 在训练环境中将 `*.pt` 导出为 `*.onnx`
2. 在 OpenExplorer Docker 或对应 Python 环境中将 `*.onnx` 转为 `*.bin`
3. 生成供板端推理使用的量化模型与转换日志

### 2. 端侧推理

推理入口位于 [model_infer/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/README.md)。

典型流程为：

1. 使用交叉编译工具链构建 `model_infer`
2. 将可执行程序、模型文件和依赖库部署到 RDK 板端
3. 运行测试程序并输出检测结果与可视化图片

## 结果展示

以下示例展示了仓库当前推理流程的输入图像与输出结果：

### 输入图像

![输入图像](model_infer/asset/000000440171.jpg)

### 推理结果

![推理结果](model_infer/asset/result.jpg)

## 依赖与环境

### 模型转换侧

- Python 3
- Ultralytics / PyTorch
- ONNX Runtime
- OpenCV Python
- `hb_mapper`

### 推理侧

- `aarch64-linux-gnu-gcc` / `aarch64-linux-gnu-g++`
- RDK DNN SDK
- OpenCV aarch64 安装包
- RDK 板端运行环境

## 子模块说明

- [model_convert/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_convert/README.md)：介绍模型导出与模型转换的职责、输入输出和使用流程
- [model_infer/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/README.md)：介绍交叉编译、板端运行和推理接口

## 说明

本仓库当前文档重点覆盖模型转换与模型推理主流程，不展开性能优化、日志采集或更细粒度模块实现。若需要查看底层封装，可继续阅读 [model_infer/model_inference_base/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/model_inference_base/README.md)。
