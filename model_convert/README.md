# model_convert 模块说明

`model_convert` 负责将训练阶段产出的通用模型逐步转换为 RDK 板端可直接部署的二进制模型，是整个部署链路中的模型准备模块。

## 模块职责

本目录分为两个阶段：

- `pt2onnx/`：将 Ultralytics 训练得到的 `*.pt` 模型导出为 `*.onnx`
- `onnx2bin/`：将浮点 ONNX 模型量化并编译为 RDK 平台可运行的 `*.bin`

对应关系如下：

```text
*.pt
  -> pt2onnx/rdk_pt2onnx.py
*.onnx
  -> onnx2bin/mapper.py
*.bin
```

## 目录说明

```text
model_convert/
|-- pt2onnx/
|   |-- rdk_pt2onnx.py      # PT 导出 ONNX 脚本
|   |-- yolo11m.pt          # 示例训练模型
|   `-- yolo11m.onnx        # 示例导出结果
`-- onnx2bin/
    |-- mapper.py           # ONNX 转 BIN 脚本
    |-- calibration_data_img/
    |-- yolo11m.onnx
    |-- yolo11m_bayese_640x640_nv12.bin
    `-- hb_mapper_makertbin.log
```

## 一、PT 导出 ONNX

### 功能说明

[pt2onnx/rdk_pt2onnx.py](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_convert/pt2onnx/rdk_pt2onnx.py) 运行在 Ultralytics 训练环境中，用于：

- 加载 `*.pt` 模型
- 对部分算子结构进行适配
- 导出更适合 RDK 工具链处理的 ONNX 模型

脚本中对 `Detect`、`Segment`、`Pose`、`Attention` 等模块做了 monkey patch，目标是让导出结构更适合后续 BPU 编译。

### 典型用法

```bash
cd model_convert/pt2onnx
python3 rdk_pt2onnx.py --pt ./yolo11m.pt --optse 11
```

### 输入与输出

- 输入：`*.pt`
- 输出：与原模型同目录的 `*.onnx`

### 注意事项

- 该脚本应在训练环境中执行，而不是板端执行
- 当前导出图像尺寸固定为 `640 x 640`
- 若使用自定义模型结构，需要确认 patch 逻辑是否仍然适用

## 二、ONNX 转 BIN

### 功能说明

[onnx2bin/mapper.py](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_convert/onnx2bin/mapper.py) 运行在 OpenExplorer Docker 或相应 Python 环境中，用于：

- 校验 ONNX 输入格式
- 构造校准数据
- 生成 `hb_mapper` 配置
- 调用 `hb_mapper makertbin`
- 输出量化后的 `*.bin` 模型和转换日志

脚本默认使用：

- `march: bayes-e`
- `input_type_rt: nv12`
- `input_layout_train: NCHW`
- 输出模型名格式：`<model_name>_bayese_<width>x<height>_nv12.bin`

### 典型用法

```bash
cd model_convert/onnx2bin
python3 mapper.py \
  --onnx ./yolo11m.onnx \
  --cal-images ./calibration_data_img \
  --output-dir .
```

### 常用参数

- `--onnx`：输入 ONNX 模型路径
- `--cal-images`：校准图片目录
- `--output-dir`：输出目录
- `--quantized`：量化模式，默认 `int8`
- `--jobs`：编译并行数
- `--optimize-level`：优化等级，默认 `O3`
- `--cal-sample-num`：采样校准图片数量，默认 `100`

### 输入与输出

- 输入：单输入、`NCHW`、`float32` 的 ONNX 模型
- 输入：校准图片目录，建议 `20~50` 张以上
- 输出：`*.bin`
- 输出：`hb_mapper_makertbin.log`

### 注意事项

- ONNX 模型必须是单输入模型
- 输入数据类型必须为 `tensor(float)`
- 校准图片为空或数量过少时，转换质量和稳定性会受到影响
- 脚本会生成临时工作目录，默认在转换结束后自动清理

## 推荐流程

```bash
cd model_convert/pt2onnx
python3 rdk_pt2onnx.py --pt ./yolo11m.pt --optse 11

cd ../onnx2bin
python3 mapper.py \
  --onnx ../pt2onnx/yolo11m.onnx \
  --cal-images ./calibration_data_img \
  --output-dir .
```

## 与推理模块的关系

`model_convert` 的最终产物是 `*.bin` 模型文件，该文件会被 [model_infer/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/README.md) 中的推理程序加载并在 RDK 板端执行。
