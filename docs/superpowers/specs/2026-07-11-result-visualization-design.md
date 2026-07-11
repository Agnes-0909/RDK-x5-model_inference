# 双路推理结果可视化保存设计

## 目标

为 `mipi_vse_bpu_zerocopy` 增加默认关闭、运行时可配置的 JPEG 可视化保存功能。开启后分别保存左路、右路带检测框结果，并额外保存同一处理序号的左右拼接图。检测标签显示 `class_id + score`。

该功能不得改变 BPU 输入路径：VSE 物理地址仍直接注册并绑定为 DNN 输入。可视化需要的图像复制只允许发生在 BPU 推理和后处理成功之后，并明确归类为可选的结果导出路径。

## 用户配置

新增应用参数：

```text
--visualize                 开启可视化，默认关闭
--visual-dir PATH           输出目录；未指定时使用 <log-dir>/visual
--visual-interval N         每 N 个成功结果采样一次，默认 10，必须 > 0
--visual-max-frames N       每路最多保存的采样帧数，默认 100，必须 > 0
--visual-quality N          JPEG 质量，默认 90，范围 1..100
```

运行脚本增加 `VISUALIZE`、`VISUAL_DIR`、`VISUAL_INTERVAL`、`VISUAL_MAX_FRAMES` 和 `VISUAL_QUALITY` 环境变量。只有 `VISUALIZE=1` 时脚本才传递可视化参数，保持默认运行无额外复制和线程。

## 输出结构

```text
<visual-dir>/
  left/left_000001_frame_123.jpg
  right/right_000001_frame_456.jpg
  combined/pair_000001_left_123_right_456.jpg
```

序号是每路成功处理结果的从 1 开始序号，不是硬件 frame ID。左右 worker 都按 FIFO 处理 coordinator 成对投递的帧，因此第 k 个成功结果组成第 k 对。任一路失败会终止运行，不继续生成可能错位的 combined 图。

## 组件设计

### VisualizationManager

新增独立 `VisualizationManager`，由主程序创建并同时传给左右 `ZeroCopyChannelProcessor`。它负责：

- 配置校验和目录建立。
- 接收包含 channel、成功序号、硬件 frame ID、640×640 NV12 副本和 detections 的 `VisualizationSnapshot`。
- 使用一个有界阻塞队列和一个 writer 线程，避免磁盘写入发生在推理 worker 内。
- 将 NV12 转成 RGB，画矩形框及 `class_id score` 标签。
- 保存单路 JPEG；缓存同成功序号的左右 RGB/元数据，两路到齐后生成 1280×640 拼接 JPEG。
- shutdown 时排空队列、完成所有可配对图、报告未配对任务和写盘错误。

队列满时阻塞采样 worker，不丢弃已经决定保存的任务。阻塞时间、转换时间、JPEG 时间、单路/拼接成功数和错误数进入日志和 summary。由于开启可视化会增加复制、CPU 和 I/O 开销，开启/关闭运行的性能数据不得直接比较。

### 图像快照和帧生命周期

`ZeroCopyChannelProcessor::process` 在 runner 返回 detections 并完成物理地址一致性检查后执行采样判断。命中采样时：

1. 验证 layout 为 640×640 NV12。
2. 在 VSE frame 有效期间逐行复制 Y/UV plane 到独立、紧凑的 614400-byte NV12 buffer。
3. 复制 detections 元数据并提交 manager。
4. 返回 worker；worker 随后按现有逻辑释放 VSE frame。

writer 线程不持有 VSE 地址、frame guard 或 SDK 句柄。可视化关闭或未命中采样条件时，不执行图像复制。

### JPEG 编码

X5 板端存在 `libturbojpeg.so`，但当前交叉编译环境没有对应 aarch64 开发库。工程不新增静态依赖，而是封装 `TurboJpegEncoder`，通过 `dlopen("libturbojpeg.so.0")` 和 `dlsym` 加载所需 ABI。初始化时验证全部符号；失败时返回可读错误。

编码输入为自有 RGB buffer，输出使用 TurboJPEG 分配/释放接口或调用方提供的最大缓冲。JPEG 文件先写同目录临时文件，成功关闭后原子 rename 到最终文件，避免留下半文件。

### 标注绘制

- 检测框坐标裁剪到 640×640。
- 不同 `class_id` 使用确定性颜色。
- 线宽 2 像素。
- 标签格式为 `<class_id> <score-with-2-decimals>`。
- 使用工程内置的小型 5×7 ASCII 字体，只覆盖数字、空格、负号和小数点，无外部字体依赖。
- combined 图直接左右拼接已经标注的 640×640 RGB 图，不重复绘制。

## 初始化和退出顺序

```text
logger
-> VisualizationManager（仅启用时，加载 TurboJPEG、建目录、启动 writer）
-> 顺序初始化 left/right runner
-> VIO
-> workers
-> 采集和处理
-> close/join workers
-> shutdown processors / unregister cache
-> stop VIO
-> close/drain/join VisualizationManager
-> 写 summary
-> logger shutdown
```

processor 在 manager shutdown 前销毁，writer 只访问 snapshot 自有内存。异常路径同样 close workers、注销缓存、停止 VIO，再排空或终止可视化；首个 JPEG 错误进入共享 fatal 状态，后续任务停止接收。

## 日志与统计

新增稳定事件：

- `visualization.started/stopped`
- `visualization.snapshot_submitted`
- `visualization.single_saved`
- `visualization.combined_saved`
- `visualization.snapshot_failed`

字段包含 channel、success sequence、frame ID、路径、copy/queue/convert/encode/write 耗时及错误。`summary.json` 新增 visualization 对象：enabled、submitted、left_saved、right_saved、combined_saved、errors、unpaired、累计等待与处理耗时。

## 测试策略

宿主测试先行覆盖：

- 配置默认值、参数解析、非法 interval/max/quality/path。
- 4×4/8×8 NV12 到 RGB 的已知像素转换和裁剪。
- box 裁剪、颜色稳定性、线宽和 `class_id score` 字符绘制。
- 左右相同序号配对、乱序到达、单路输出及 combined 尺寸/像素布局。
- interval/max 采样策略。
- 有界队列阻塞、shutdown 排空、错误传播和未配对统计。
- TurboJPEG 动态加载失败路径；板端验证真实 JPEG 可解码。
- 可视化关闭时不调用 snapshot copy/submit。
- 现有 `no_image_copy` 门禁继续覆盖推理路径，并明确允许只在独立 visualization 源文件中出现可视化复制。

Docker 交叉构建后在 X5 执行：左右各 5 个采样结果，验证 left/right 各 5 张、combined 5 张、JPEG magic/尺寸正确、框可见、summary 数量一致、200 次零拷贝地址证据不受影响。再分别验证关闭可视化时不创建 visual 目录，以及 interval/max 生效。

## 验收标准

- 默认关闭时现有行为和零拷贝性能路径不变。
- 开启时左、右和 combined JPEG 数量与采样策略一致，文件可解码。
- 检测框坐标、标签及拼接方向正确。
- VSE frame 在快照复制后按原顺序释放，writer 不访问 VSE 内存。
- JPEG/目录/写盘错误可见并触发安全退出，不留下半文件。
- 日志和 summary 可解释可视化带来的复制、等待、编码与 I/O 开销。
