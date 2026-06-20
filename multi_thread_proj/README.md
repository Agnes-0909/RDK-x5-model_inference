# multi_thread_proj

`multi_thread_proj` 是构建在 [model_infer/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/model_infer/README.md) 之上的多线程服务化推理子工程，用于把单模型端侧推理能力封装成面向多路输入的并发执行链路。

它当前面向固定 3 路输入通道，使用：

- 共享内存传递图像负载
- Unix Domain Socket 传递批次控制消息
- 按通道绑定的 worker 执行推理
- 聚合器汇总多通道结果后统一回传

## 架构概览

整体由两个进程和一组公共组件组成：

- `mt_manager`：负责收集输入、准备帧数据、写入共享内存，并通知服务端开始处理
- `mt_server`：负责接收批次通知、按通道调度 worker、执行推理并返回聚合结果
- `SharedMemoryQueue`：负责管理跨进程共享内存中的多通道帧槽位
- `UdsSocket`：负责管理端和服务端之间的控制消息收发
- `ResultAggregator`：负责等待每个通道完成后拼装 `BatchResultMessage`
- `Worker`：每个启用通道对应一个 worker 线程，负责从共享内存取帧并调用 `rdk::YoloInfer`

```text
input-dir-0/1/2
    -> mt_manager
    -> SharedMemoryQueue::push()
    -> NotifyMessage over UDS
    -> mt_server
    -> worker[channel]
    -> rdk::YoloInfer
    -> ResultAggregator
    -> BatchResultMessage over UDS
    -> mt_manager
```

## 通信流程

一次完整批次的处理链路如下：

1. `mt_manager` 扫描 `--input-dir-0`、`--input-dir-1`、`--input-dir-2`，为每个通道准备输入图像
2. 管理端把每路帧数据编码成共享内存中的一个槽位，并写入 `FrameMetadata`
3. 管理端构造 `NotifyMessage`，其中包含：
   - `batch_id`
   - `notify_timestamp_ns`
   - `channel_mask`
   - 每个通道对应的 `frame_id`
4. `mt_manager` 通过 Unix Domain Socket 把 `NotifyMessage` 发送给 `mt_server`
5. `mt_server` 根据 `channel_mask` 为启用通道创建 `WorkItem`，并投递给对应 worker
6. worker 从 `SharedMemoryQueue` 按 `channel_id + frame_id` 获取帧数据，调用 `rdk::YoloInfer::infer()`
7. 每个 worker 产出 `ChannelResult` 后提交给 `ResultAggregator`
8. `ResultAggregator` 等待该批次所有目标通道完成后，拼装 `BatchResultMessage`
9. `mt_server` 通过 Unix Domain Socket 把聚合结果回传给 `mt_manager`

这里的数据通道和控制通道是分离的：

- 大块图像数据走共享内存，减少跨进程重复拷贝
- 小体积调度消息走 UDS，便于保持控制流简单和同步

## 协议与数据结构

核心协议定义位于 [include/protocol.hpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/include/protocol.hpp)。

当前协议特点：

- 固定协议魔数 `kProtocolMagic`
- 协议版本 `kProtocolVersion = 1`
- 固定通道数 `kChannelCount = 3`
- 每通道最大检测结果数 `kMaxDetectionsPerChannel = 256`
- 默认帧尺寸 `2592 x 1944`
- 默认模型输入尺寸 `640 x 640`

关键消息有两个：

- `NotifyMessage`：管理端发送给服务端，表示一个 batch 已经准备完毕
- `BatchResultMessage`：服务端返回给管理端，表示 batch 聚合结果已经准备完毕

共享内存帧元数据定义位于 [include/shared_memory_queue.hpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/include/shared_memory_queue.hpp)，包含：

- `batch_id`
- `channel_id`
- `frame_id`
- 图像宽高和负载大小
- crop 信息
- source 宽高
- letterbox 参数
- 是否已经预处理
- 槽位状态 `SlotStatus`

## 模块职责

### `include/`

- [protocol.hpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/include/protocol.hpp)：批次通知与结果回传协议
- [shared_memory_queue.hpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/include/shared_memory_queue.hpp)：共享内存队列接口和帧元数据
- [server_types.hpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/include/server_types.hpp)：通道推理结果状态码和记录结构
- `image_codec.hpp`、`logger.hpp`、`uds_socket.hpp`：图像编解码、日志和 UDS 通信公共接口

### `src/common/`

公共基础组件：

- `shared_memory_queue.cpp`：共享内存布局、槽位读写、状态切换
- `protocol.cpp`：协议相关实现
- `image_codec.cpp`：输入图像编码与传输辅助逻辑
- `uds_socket.cpp`：服务端监听、客户端连接、定长消息收发
- `logger.cpp`：统一日志输出

### `src/manager/`

管理端入口和调度逻辑：

- [manager_main.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/src/manager/manager_main.cpp)：命令行入口
- [manager_app.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/src/manager/manager_app.cpp)：参数解析、输入收集、共享内存写入、批次通知和结果接收

管理端负责：

- 收集 3 路输入目录中的文件
- 准备每路待推理帧
- 为每个通道分配递增的 `frame_id`
- 向一个或两个服务端 socket 分发 `NotifyMessage`
- 接收并合并返回结果

### `src/server/`

服务端入口、worker 和结果聚合逻辑：

- [server_main.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/src/server/server_main.cpp)：命令行入口
- [server_app.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/src/server/server_app.cpp)：参数解析、共享内存初始化、socket 监听、batch 分发和结果发送
- [worker.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/src/server/worker.cpp)：单通道 worker 线程、共享内存取帧、模型推理和日志记录
- [result_aggregator.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/src/server/result_aggregator.cpp)：等待批次内所有通道完成并构造聚合结果

服务端负责：

- 创建共享内存和 UDS 监听端点
- 根据 `channel_mask` 启用目标通道
- 为每个通道初始化独立 worker
- 调用 `rdk::YoloInfer` 执行推理
- 生成 `BatchResultMessage` 并回传

## 构建方式

### 依赖

该子工程依赖：

- 根仓库下的 `model_infer/`
- `3rdlibrary/DNN`
- `3rdlibrary/opencv_aarch64`
- `aarch64-linux-gnu-gcc` / `aarch64-linux-gnu-g++`
- CMake 3.14+

[CMakeLists.txt](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/CMakeLists.txt) 会自动把 `model_infer` 作为子目录引入，并链接 `yolo_infer` 和 `model_inference_base`。

### 构建命令

```bash
cd multi_thread_proj
./build.sh
```

默认行为：

- 清理并重建 `build_aarch64/`
- 生成 `aarch64-linux-gnu.toolchain.cmake`
- 使用交叉编译工具链执行 CMake 配置和构建

默认输出包括：

- `build_aarch64/mt_server`
- `build_aarch64/mt_manager`
- `build_aarch64/test_*`

可用环境变量：

- `BUILD_DIR`
- `BUILD_TYPE`
- `DNN_ROOT`
- `OpenCV_DIR`
- `BUILD_JOBS`

## 运行参数

### `mt_server`

关键参数：

- `--model <path>`：模型路径
- `--uds-path <path>`：UDS socket 路径
- `--shm-path <path>`：共享内存路径
- `--channel-mask <mask>`：启用通道位图
- `--bpu-core-id <id>`：BPU core 选择

### `mt_manager`

关键参数：

- `--input-dir-0 <path>`
- `--input-dir-1 <path>`
- `--input-dir-2 <path>`
- `--uds-path <path>`
- `--uds-path-secondary <path>`：可选的第二个服务端连接
- `--shm-path <path>`

当前工程里的板端运行和日志回收，一般通过 [skills/README.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/skills/README.md) 中说明的 `scripts/perf/` 适配脚本来完成，而不是手工逐条执行。

## 测试

当前测试位于 `test/`，主要覆盖协议、共享内存、参数解析和聚合逻辑：

- [test_protocol.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_protocol.cpp)：协议常量和消息结构基本约束
- [test_shared_memory_queue.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_shared_memory_queue.cpp)：共享内存队列基本行为
- [test_result_aggregator.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_result_aggregator.cpp)：批次聚合逻辑
- [test_manager_result_merge.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_manager_result_merge.cpp)：管理端结果合并逻辑
- [test_server_args.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_server_args.cpp)：服务端参数解析
- [test_logger.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_logger.cpp)：日志组件
- [test_image_codec_transport.cpp](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/multi_thread_proj/test/test_image_codec_transport.cpp)：图像编码传输相关逻辑

构建后可在 `build_aarch64/` 下直接执行这些测试二进制。

## 与主仓库的关系

`multi_thread_proj` 不是一个完全独立的子仓库，它直接复用主仓库已有的推理实现和第三方依赖：

- 依赖 `model_infer` 提供 `yolo_infer` 和 `model_inference_base`
- 依赖根目录 `3rdlibrary/` 中的 DNN SDK 与 OpenCV
- 通常配合根目录的 `scripts/perf/` 和 `perf/` 完成板端运行与性能分析

如果要把这个子工程单独迁移出去，至少需要同时迁移：

- `model_infer/` 中的相关推理目标
- `3rdlibrary/` 中对应依赖
- 构建脚本与板端部署脚本
