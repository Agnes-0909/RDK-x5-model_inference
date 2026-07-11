# mipi_vse_bpu_zerocopy

RDK X5 GS130W 双目真零拷贝推理工程。左右 SC132GS 图像分别经过 MIPI host 0/2、VIN、ISP 和 VSE；VSE 把中心 `1088x1088` 区域硬件缩放成模型需要的 `640x640 NV12`。程序注册 VSE 缓冲区物理地址并直接作为 BPU 输入，不复制图像数据。

## 构建

仓库在 Docker `3c543521b4ca` 的 `/project` 下挂载时：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

隔离工作树开发时路径为：

```bash
docker exec 3c543521b4ca bash -lc \
  'cd /project/.worktrees/mipi-vse-bpu-zerocopy/mipi_vse_bpu_zerocopy && ./build.sh'
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

默认模型是 `/root/mipi_proj/model/model.bin`，`FRAMES=0` 表示持续运行。其他参数可通过 `./mipi_vse_bpu_zerocopy --help` 查看。

## 零拷贝证据

每次推理日志同时输出 `vse_phy` 和 `dnn_phy`，两者必须完全相等。`cache_hit=0` 只会在 VSE 缓冲池中首次出现某个物理地址时出现，之后相同地址均为 `cache_hit=1`。工程的 `no_image_copy` 测试禁止推理路径使用 `hbSysWriteMem`、`memcpy` 或 OpenCV resize。

如果 VSE 输出不是严格的 640×640、stride 640、两 plane 物理连续 NV12，程序会退出并给出具体原因，不会退化到复制路径。

## 常见问题

- `hbSysRegisterMem failed`：确认板端 DNN Runtime 与 SDK 匹配，并检查 VSE 输出物理地址和长度。
- `VSE plane byte sizes` 或 `stride` 错误：查看当前板端 VPF 的 buffer 对齐方式；工程有意不做复制兜底。
- `hbn_camera_create` 或 VIN 初始化失败：确认 GS130W 已连接到 host 0/2，且没有其他进程占用 camera/VIO。
- 连续取帧超时：检查 MIPI 排线、电源、传感器时钟和内核日志。
