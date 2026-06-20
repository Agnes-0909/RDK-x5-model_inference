# 自建 Skills 接入说明

本文档说明本仓库 `skills/` 与 `scripts/` 的职责，以及如何把这套自建 skill 接入到其他人的工程和环境中。

## 目标

本仓库当前提供的自建 skill 是：

- `skills/iterative-performance-optimization/`

它的目标不是直接优化某一个固定项目，而是为“本地构建 + 板端部署 + 远程运行 + 拉取日志 + 基于日志迭代优化”提供一套可复用的工作流。  
其中，skill 负责约束优化流程，`scripts/perf/` 负责把这个流程适配到具体项目。

## 目录结构

```text
skills/
|-- README.md
`-- iterative-performance-optimization/
    |-- SKILL.md
    |-- agents/
    `-- references/

scripts/
`-- perf/
    |-- build.sh
    |-- deploy.sh
    |-- env.sh
    |-- pull_logs.sh
    `-- run.sh
```

相关的底层工程脚本位于：

```text
perf/
|-- build.sh
|-- scp_exec_and_stop.sh
`-- scp_log_from_board.sh
```

这里的分层关系是：

- `skills/iterative-performance-optimization/SKILL.md`：定义优化流程、输入要求和风险边界
- `skills/iterative-performance-optimization/references/*.md`：定义脚本契约、日志要求和优化优先级
- `scripts/perf/*.sh`：对当前工程的适配层，负责把 skill 需要的动作映射到真实项目脚本
- `perf/*.sh`：当前仓库已有的底层构建、板端执行和日志拉取实现

## 当前工程里的接法

这套 skill 在本仓库中的接法是：

1. `scripts/perf/env.sh` 统一导出项目根目录、板端地址、远端目录、模型路径、日志路径和本地产物路径
2. `scripts/perf/build.sh` 调用 `perf/build.sh` 执行本地构建
3. `scripts/perf/deploy.sh` 负责把 `mt_server` 和 `mt_manager` 部署到板端
4. `scripts/perf/run.sh` 负责在板端启动推理服务、等待运行、停止进程并保留日志
5. `scripts/perf/pull_logs.sh` 调用 `perf/scp_log_from_board.sh` 把日志回收到本地

这意味着 skill 自身不需要知道你的工程如何编译、如何登录板子、如何运行程序。它只依赖 `scripts/perf/` 这组统一入口。

## 接入到其他工程时必须保留的部分

如果别人要在自己的环境和工程中复用这套 skill，下面这些内容必须保留：

1. `skills/iterative-performance-optimization/` 整个目录
2. `scripts/perf/build.sh`
3. `scripts/perf/deploy.sh`
4. `scripts/perf/run.sh`
5. `scripts/perf/pull_logs.sh`

推荐同时保留：

6. `scripts/perf/env.sh`
7. `skills/iterative-performance-optimization/references/` 下的参考文档

## 接入步骤

### 1. 复制目录

把下面两个目录复制到目标工程：

- `skills/`
- `scripts/perf/`

推荐目标工程至少具备如下结构：

```text
your_project/
|-- skills/
|   `-- iterative-performance-optimization/
|-- scripts/
|   `-- perf/
`-- <your source tree>
```

### 2. 改造 `scripts/perf/env.sh`

`env.sh` 是最先要改的文件。它负责声明这个 skill 如何找到你的工程产物和目标设备。

至少要按你的项目修改这些变量：

- `PROJECT_ROOT`
- `BOARD_HOST`
- `BOARD_BASE_DIR`
- `BOARD_BIN_DIR`
- `BOARD_LIB_DIR`
- `BOARD_MODEL_PATH`
- `BOARD_LOG_DIR`
- `LOCAL_LOG_DIR`

如果你的程序不是本仓库这种 `server + manager` 双进程模式，还要替换这些变量：

- `SERVER_BINARY_LOCAL`
- `MANAGER_BINARY_LOCAL`
- `SERVER_BINARY_REMOTE`
- `MANAGER_BINARY_REMOTE`
- `SERVER_PROCESS_NAME`
- `MANAGER_PROCESS_NAME`

如果你的工程没有共享内存、UDS、BPU Core 这些概念，可以删除或替换：

- `BOARD_UDS_PATH`
- `BOARD_SHM_PATH`
- `BOARD_BPU_CORE_PRIMARY`

原则很简单：`env.sh` 里只保留你自己工程真正需要的运行参数。

### 3. 改造 `scripts/perf/build.sh`

`build.sh` 必须满足 skill 的最小契约：

- 能在本地非交互执行
- 构建失败时返回非零退出码
- 产物路径可预测

在本仓库里，它只是转调 `perf/build.sh`。  
在其他工程里，你可以把它改成任何真实构建命令，例如：

```bash
#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

cmake -S "${PROJECT_ROOT}" -B "${PROJECT_ROOT}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${PROJECT_ROOT}/build" --parallel
```

### 4. 改造 `scripts/perf/deploy.sh`

`deploy.sh` 的职责是把运行所需文件送到板端。  
最少要处理：

- 可执行文件
- 动态库
- 配置文件
- 模型文件

它需要满足：

- 用 `scp` 或等价方式传输
- 缺文件时明确报错
- 不要删除无关远端文件

如果你的工程只有一个可执行文件，就把本仓库中双二进制部署逻辑收敛成单程序部署即可。

### 5. 改造 `scripts/perf/run.sh`

`run.sh` 的职责是远程执行 workload，并把日志留在固定位置。

它需要满足：

- 用 `ssh` 或等价方式远程执行
- 执行失败时返回非零退出码
- 日志输出路径固定
- 退出前保证日志可被回收

如果你的程序是前台执行，就直接远程启动并等待结束；  
如果你的程序是后台服务，要像本仓库一样明确处理：

- 启动命令
- 就绪检查
- 运行时长
- 停止命令
- 日志文件位置

### 6. 改造 `scripts/perf/pull_logs.sh`

`pull_logs.sh` 的职责只有一个：把远端日志稳定拉回本地。

它需要满足：

- 本地输出目录固定
- 远端日志缺失时明确失败
- 不要无提示地混入旧日志

如果你已经有现成的日志拉取脚本，可以像本仓库一样在这里包一层调用。

## 脚本契约

这套 skill 默认要求目标工程提供以下脚本：

- `scripts/perf/build.sh`
- `scripts/perf/deploy.sh`
- `scripts/perf/run.sh`
- `scripts/perf/pull_logs.sh`

可选脚本：

- `scripts/perf/analyze.sh`
- `scripts/perf/env.sh`

更完整的契约说明见：

- [adapter-contract.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/skills/iterative-performance-optimization/references/adapter-contract.md)
- [optimization-checklist.md](/Users/xurongtang/workerFolder/RDK_resource/RDK_proj/skills/iterative-performance-optimization/references/optimization-checklist.md)

## 目标工程需要满足什么条件

别人要在自己的工程里接入这套 skill，至少要满足这些前提：

1. 可以在本地完成构建
2. 可以通过 `ssh` / `scp` 与目标板通信，或提供等价能力
3. 运行过程能产生日志
4. 日志里能看出耗时阶段，或者允许先补日志再做优化
5. 项目团队接受用脚本驱动性能优化流程，而不是每次手工执行

如果一个工程连“怎么构建、怎么部署、怎么运行、日志在哪”都无法脚本化，这套 skill 就不适合直接接入。

## 推荐接入流程

对外复用时，建议按下面顺序接：

1. 先复制 `skills/` 与 `scripts/perf/`
2. 改 `env.sh`，让路径、设备和日志目录都指向目标工程
3. 单独验证 `build.sh`
4. 单独验证 `deploy.sh`
5. 单独验证 `run.sh`
6. 单独验证 `pull_logs.sh`
7. 最后再在 agent 环境里调用 `iterative-performance-optimization`

这样做的原因很直接：大多数接入问题都出在脚本适配层，而不是 skill 文本本身。

## 接入后的使用方式

当别人把这套目录接到自己的工程后，agent 在执行性能优化任务时，应显式使用：

- `iterative-performance-optimization`

这个 skill 会要求先检查适配脚本，再建立基线日志，再选择一次只做一个优化点的迭代方式。

## 建议

如果后续要把这套自建 skill 做成团队级复用能力，建议继续补两类内容：

1. 为不同工程模板提供不同版本的 `scripts/perf/env.sh`
2. 增加 `scripts/perf/analyze.sh`，把日志首轮分析标准化
