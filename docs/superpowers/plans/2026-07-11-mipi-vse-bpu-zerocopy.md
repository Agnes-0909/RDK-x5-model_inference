# MIPI VSE BPU Zero-Copy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and validate an independent RDK X5 application that runs both GS130W camera streams through ISP and VSE, then binds each VSE-owned 640x640 NV12 buffer directly to the YOLO11m BPU input without copying image bytes.

**Architecture:** Two fixed SC132GS VFlow pipelines produce center-cropped 640x640 NV12 frames. A validated registration cache maps each VSE physical buffer once with `hbSysRegisterMem`; one synchronous DNN runner aliases that external memory as its input, waits for BPU completion, postprocesses YOLO11 outputs, and only then releases the frame.

**Tech Stack:** C++17, CMake, RDK X5 Camera/VIO API, hbmem, Horizon DNN Runtime, aarch64-linux-gnu, Docker, SSH.

---

## File structure

- `CMakeLists.txt`, `build.sh`: independent host tests and aarch64 build.
- `include/app_config.hpp`, `src/app_config.cpp`: defaults and validated CLI.
- `include/detection*.hpp`, `src/detection_mapper.cpp`: data types and 640-to-sensor mapping.
- `include/buffer_layout.hpp`, `src/buffer_layout.cpp`: strict VSE memory checks.
- `include/registered_buffer_cache.hpp`, `src/registered_buffer_cache.cpp`: registration lifecycle.
- `include/vse_frame.hpp`, `src/vse_frame.cpp`: move-only frame guard.
- `include/yolo11_postprocess.hpp`, `src/yolo11_postprocess.cpp`: six-head DFL decode and NMS.
- `include/zero_copy_yolo_runner.hpp`, `src/zero_copy_yolo_runner.cpp`: external-input DNN runner.
- `include/vio_pipeline.hpp`, `src/vio_pipeline.cpp`, `src/sensor/`: dual SC132GS VFlow.
- `src/main.cpp`: dual-stream lifecycle and statistics.
- `test/`: host tests; `scripts/run_on_board.sh`: board acceptance runner.

### Task 1: Build skeleton and configuration

**Files:** Create `CMakeLists.txt`, `build.sh`, `include/app_config.hpp`, `src/app_config.cpp`, `test/test_app_config.cpp`.

- [ ] Write the failing test:

```cpp
int main() {
  auto c = zerocopy::defaultConfig();
  CHECK(c.model_path == "/root/mipi_proj/model/model.bin");
  CHECK(c.frames == 100 && c.left_host == 0 && c.right_host == 2);
  const char* a[] = {"app", "--frames", "0", "--score", "0.4"};
  c = zerocopy::parseArgs(5, const_cast<char**>(a));
  CHECK(c.frames == 0 && c.score_threshold == 0.4F);
}
```

- [ ] Run RED: `cmake -S mipi_vse_bpu_zerocopy -B /tmp/mipi-zc-host -DBUILD_HARDWARE_APP=OFF && cmake --build /tmp/mipi-zc-host --target test_app_config`. Expect missing header/functions.
- [ ] Implement `AppConfig` with model path, frames, hosts 0/2, frame timeout 2000 ms, five consecutive timeouts, DNN timeout 5000 ms, score 0.25 and NMS 0.45. Reject negative counts, duplicate hosts, nonpositive timeouts and thresholds outside `(0,1)`.
- [ ] Run GREEN: `cmake --build /tmp/mipi-zc-host -j && /tmp/mipi-zc-host/test_app_config`. Expect `PASS`.
- [ ] Commit: `git add mipi_vse_bpu_zerocopy && git commit -m "build: scaffold mipi vse bpu zero-copy project"`.

### Task 2: Geometry and strict buffer validation

**Files:** Create `include/detection.hpp`, `include/detection_mapper.hpp`, `src/detection_mapper.cpp`, `include/buffer_layout.hpp`, `src/buffer_layout.cpp`, `test/test_geometry_and_layout.cpp`.

- [ ] Write failing tests:

```cpp
CHECK(mapModelBoxToSensor({0,0,640,640}) == Box{0,96,1088,1280});
BufferLayout ok{640,640,640,640,409600,204800,0x100000,0x164000,2,PixelFormat::Nv12};
CHECK(validateZeroCopyLayout(ok).total_bytes == 614400);
ok.uv_phy = 0x170000;
CHECK_THROWS_WITH(validateZeroCopyLayout(ok), "UV physical address is not contiguous with Y");
```

- [ ] Run RED: `cmake --build /tmp/mipi-zc-host --target test_geometry_and_layout`. Expect missing APIs.
- [ ] Implement mapping with sensor 1088x1280, crop `(0,96,1088,1088)`, model 640. Validate format, plane count, dimensions, Y/UV stride 640, exact plane sizes, nonzero addresses, adjacency and total bytes; never return a fallback.
- [ ] Run GREEN: `cmake --build /tmp/mipi-zc-host -j && /tmp/mipi-zc-host/test_geometry_and_layout`. Expect `PASS`.
- [ ] Commit the geometry/layout files with `git commit -m "feat: validate zero-copy VSE layouts"`.

### Task 3: Registration cache and frame guard

**Files:** Create `include/registered_buffer_cache.hpp`, `src/registered_buffer_cache.cpp`, `include/vse_frame.hpp`, `src/vse_frame.cpp`, `test/test_resource_lifecycle.cpp`.

- [ ] Write failing injected-lifecycle tests:

```cpp
int reg=0, unreg=0, release=0;
RegisteredBufferCache cache([&](ExternalMemory&){++reg;return 0;},
                            [&](ExternalMemory&){++unreg;return 0;});
cache.getOrRegister(valid); cache.getOrRegister(valid);
CHECK(reg == 1); cache.clear(); CHECK(unreg == 1);
{ VseFrame f(7, [&]{++release;return 0;}); }
CHECK(release == 1);
```

- [ ] Run RED: `cmake --build /tmp/mipi-zc-host --target test_resource_lifecycle`.
- [ ] Implement cache-by-Y-physical-address. Validate before registration, reject changed virtual address/size on a cache hit, try every unregister in `clear()`, and never free VSE memory. Make `VseFrame` noncopyable, movable, and at-most-once release.
- [ ] Run GREEN: `cmake --build /tmp/mipi-zc-host -j && ctest --test-dir /tmp/mipi-zc-host --output-on-failure`.
- [ ] Commit with `git commit -m "feat: manage registered VSE frame lifetimes"`.

### Task 4: YOLO11 six-head postprocessing

**Files:** Create `include/yolo11_postprocess.hpp`, `src/yolo11_postprocess.cpp`, `test/test_yolo11_postprocess.cpp`.

- [ ] Write synthetic float-NHWC test data for `(80x80x80,80x80x64)`, `(40x40x80,40x40x64)`, `(20x20x80,20x20x64)`. Give one grid cell a high class logit and one dominant DFL bin per side; assert class, sigmoid score, original-image box, and class-aware NMS.
- [ ] Run RED: `cmake --build /tmp/mipi-zc-host --target test_yolo11_postprocess`. Expect missing processor.
- [ ] Implement `FloatTensorView {data,height,width,channels}`. Pair 80/64-channel heads by size, use strides 8/16/32, inverse-sigmoid threshold, 16-bin DFL softmax, coordinate mapper, descending class-aware NMS, and strict shape rejection.
- [ ] Run GREEN: `cmake --build /tmp/mipi-zc-host -j && /tmp/mipi-zc-host/test_yolo11_postprocess`.
- [ ] Commit with `git commit -m "feat: decode yolo11 multi-scale outputs"`.

### Task 5: External-input DNN runner

**Files:** Create `include/zero_copy_yolo_runner.hpp`, `src/zero_copy_yolo_runner.cpp`, `test/test_tensor_binding.cpp`.

- [ ] Write failing pure binding test:

```cpp
ModelInputSpec spec{1,3,640,640,614400};
ExternalMemory mem{0x100000,reinterpret_cast<void*>(0x200000),614400};
auto b = makeInputBinding(spec, mem);
CHECK(b.phy_addr == mem.phy_addr && b.bytes == 614400);
CHECK_THROWS(makeInputBinding(spec,{0x100000,nullptr,600000}));
```

- [ ] Run RED, implement exact size/address/type validation, then run GREEN with `cmake --build /tmp/mipi-zc-host --target test_tensor_binding && /tmp/mipi-zc-host/test_tensor_binding`.
- [ ] Implement hardware runner: load one model; validate name, NV12/NCHW/1x3x640x640/614400; allocate six outputs only. For inference, copy only the small registered `hbSysMem` descriptor into a model-derived input tensor, assert physical-address equality, infer/wait, invalidate outputs, release task, decode, and return timings. Do not call `hbSysWriteMem` or copy image bytes.
- [ ] Docker compile: `docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh --target mipi_vse_bpu_zerocopy'`.
- [ ] Commit with `git commit -m "feat: bind registered VSE memory to DNN input"`.

### Task 6: Dual SC132GS VIN–ISP–VSE pipelines

**Files:** Create `include/vio_pipeline.hpp`, `src/vio_pipeline.cpp`, `src/sensor/vp_sensors.h`, `src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c`, `test/test_vio_state_machine.cpp`.

- [ ] Write a failing injected-API rollback test: fail ISP after camera/VIN creation and assert VIN then camera cleanup once; assert successful teardown order VFlow, VSE, ISP, VIN, camera.
- [ ] Run RED: `cmake --build /tmp/mipi-zc-host --target test_vio_state_machine`.
- [ ] Deep-copy mutable SC132GS camera/MIPI/VIN definitions per pipeline. Create camera, VIN, ISP and one VSE; set VSE input 1088x1280 NV12, ROI `(0,96,1088,1088)`, target 640x640 NV12 and three contiguous cached buffers. Bind `VIN:1 -> ISP:0 -> VSE:0`, attach and start. Start host 2 then host 0, matching the proven existing order.
- [ ] Implement `acquire()` with `hbn_vnode_getframe`; return `VseFrame` whose closure releases the original image. Do not invalidate CPU cache or copy planes on inference path.
- [ ] Run GREEN host tests, then Docker build: `ctest --test-dir /tmp/mipi-zc-host --output-on-failure` and `docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'`.
- [ ] Commit with `git commit -m "feat: add dual sc132gs isp vse pipelines"`.

### Task 7: Application, copy-policy check and documentation

**Files:** Create `src/main.cpp`, `test/check_no_image_copy.cmake`, `scripts/run_on_board.sh`, `README.md`; modify `CMakeLists.txt`.

- [ ] Add a failing CTest policy check requiring `main.cpp` and rejecting `hbSysWriteMem`, `cv::resize`, or image-buffer `memcpy` in inference-path sources. Run `ctest --test-dir /tmp/mipi-zc-host -R no_image_copy`; expect missing-source failure.
- [ ] Implement main: signal handler only sets atomic flag; initialize hbmem/DNN/pipelines; acquire both frames; validate/register; infer and release left, then right; count successes/timeouts; log channel, frame ID, `vse_phy`, `dnn_phy`, cache hit and timings; unwind in documented order on error.
- [ ] Add runner exporting required library paths and README with Docker build, SCP, board run, proof fields and troubleshooting.
- [ ] Verify: `cmake --build /tmp/mipi-zc-host -j && ctest --test-dir /tmp/mipi-zc-host --output-on-failure`.
- [ ] Verify Docker: `docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'` and `file mipi_vse_bpu_zerocopy/build_aarch64/mipi_vse_bpu_zerocopy`; expect aarch64 ELF.
- [ ] Commit with `git commit -m "feat: complete dual mipi vse bpu zero-copy app"`.

### Task 8: X5 board acceptance

**Files:** Modify project files only when a board failure has first been captured and reproduced by a regression test.

- [ ] Deploy executable and runner to `/root/mipi_vse_bpu_zerocopy` with `SSHPASS=root sshpass -e scp`.
- [ ] Run two frames: `SSHPASS=root sshpass -e ssh root@192.168.1.12 'cd /root/mipi_vse_bpu_zerocopy && FRAMES=2 ./run_on_board.sh'`. Expect two successes per side and identical VSE/DNN addresses.
- [ ] For any defect, first add/observe a failing host injected-API or pure-logic test, apply the smallest fix, rerun all host tests, Docker build and two-frame smoke test.
- [ ] Run acceptance: `SSHPASS=root sshpass -e ssh root@192.168.1.12 'cd /root/mipi_vse_bpu_zerocopy && FRAMES=100 ./run_on_board.sh 2>&1 | tee acceptance.log'`. Expect 100 successes per side, zero VSE/register/DNN errors, finite stable cache size, and address equality for all 200 submissions.
- [ ] Final verification:

```bash
rg -n 'hbSysWriteMem|cv::resize|memcpy' mipi_vse_bpu_zerocopy/src mipi_vse_bpu_zerocopy/include
cmake --build /tmp/mipi-zc-host -j
ctest --test-dir /tmp/mipi-zc-host --output-on-failure
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
git diff --check
```

- [ ] Commit board-proven corrections, if any, with `git commit -m "fix: validate zero-copy pipeline on x5 hardware"`.
