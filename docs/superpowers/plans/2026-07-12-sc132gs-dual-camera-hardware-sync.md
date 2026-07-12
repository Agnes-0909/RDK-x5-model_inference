# SC132GS Dual-Camera Hardware Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make both SC132GS sensors expose continuously at 30 FPS from X5 LPWM hardware triggers and expose timestamp evidence for board validation.

**Architecture:** Keep the existing dual MIPI → VIN → ISP → VSE → BPU pipelines and independent inference workers. Put immutable trigger timing in a host-testable helper, apply it to the SDK VIN configuration, prepare both VIO flows before starting either one, and carry hardware timestamps with `VseFrame` so the acquisition loop can log stereo skew.

**Tech Stack:** C++17, C sensor configuration, Horizon X5 Camera/VIO SDK, CMake/CTest, Docker AArch64 cross-compilation, shell/SSH board validation.

---

## File map

- Modify `mipi_vse_bpu_zerocopy/include/vio_pipeline.hpp`: declare host-testable 30 FPS sync timing.
- Modify `mipi_vse_bpu_zerocopy/src/cleanup_stack.cpp`: implement timing factory without hardware SDK dependencies.
- Modify `mipi_vse_bpu_zerocopy/test/test_vio_state_machine.cpp`: verify timing and routing assumptions.
- Modify `mipi_vse_bpu_zerocopy/src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c`: switch SC132GS to slave mode and enable LPWM defaults.
- Create `mipi_vse_bpu_zerocopy/test/check_hardware_sync_config.cmake`: statically guard the sensor and runtime configuration.
- Modify `mipi_vse_bpu_zerocopy/CMakeLists.txt`: register the static hardware-sync check.
- Modify `mipi_vse_bpu_zerocopy/src/vio_pipeline.cpp`: preserve LPWM, apply timing, prepare both flows before starting them, and capture hardware timestamps.
- Modify `mipi_vse_bpu_zerocopy/include/vse_frame.hpp`: expose a frame hardware timestamp.
- Modify `mipi_vse_bpu_zerocopy/src/vse_frame.cpp`: preserve timestamp across move operations.
- Modify `mipi_vse_bpu_zerocopy/src/main.cpp`: log per-pair timestamp skew and maximum observed skew.
- Modify `mipi_vse_bpu_zerocopy/README.md`: document synchronization behavior, validation, and hardware limitation.

### Task 1: Define and test the synchronization timing contract

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/include/vio_pipeline.hpp`
- Modify: `mipi_vse_bpu_zerocopy/src/cleanup_stack.cpp`
- Test: `mipi_vse_bpu_zerocopy/test/test_vio_state_machine.cpp`

- [ ] **Step 1: Write the failing timing test**

Add assertions to `test_vio_state_machine.cpp`:

```cpp
const auto sync = zerocopy::sc132gsHardwareSyncTiming();
CHECK(sync.fps == 30);
CHECK(sync.period_us == 33333);
CHECK(sync.offset_us == 10);
CHECK(sync.duty_us == 100);
CHECK(sync.trigger_source == 0);
CHECK(sync.trigger_mode == 0);
```

- [ ] **Step 2: Run the host test and verify it fails**

Run:

```bash
cmake -S mipi_vse_bpu_zerocopy -B /tmp/mipi-sync-host -DBUILD_HARDWARE_APP=OFF
cmake --build /tmp/mipi-sync-host --target test_vio_state_machine -j4
```

Expected: compilation fails because `sc132gsHardwareSyncTiming` is not declared.

- [ ] **Step 3: Implement the timing value object**

Add to `vio_pipeline.hpp`:

```cpp
struct HardwareSyncTiming {
  int fps = 0;
  std::uint32_t period_us = 0;
  std::uint32_t offset_us = 0;
  std::uint32_t duty_us = 0;
  std::uint32_t trigger_source = 0;
  std::uint32_t trigger_mode = 0;
};

HardwareSyncTiming sc132gsHardwareSyncTiming() noexcept;
```

Include `<cstdint>`, then implement in `cleanup_stack.cpp`:

```cpp
HardwareSyncTiming sc132gsHardwareSyncTiming() noexcept {
  return {30, 33333, 10, 100, 0, 0};
}
```

- [ ] **Step 4: Run the test and verify it passes**

Run:

```bash
cmake --build /tmp/mipi-sync-host --target test_vio_state_machine -j4
/tmp/mipi-sync-host/test_vio_state_machine
```

Expected: `test_vio_state_machine: PASS`.

- [ ] **Step 5: Commit the timing contract**

```bash
git add mipi_vse_bpu_zerocopy/include/vio_pipeline.hpp \
  mipi_vse_bpu_zerocopy/src/cleanup_stack.cpp \
  mipi_vse_bpu_zerocopy/test/test_vio_state_machine.cpp
git commit -m "test: define sc132gs sync timing contract"
```

### Task 2: Configure SC132GS slave mode and LPWM

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c`
- Create: `mipi_vse_bpu_zerocopy/test/check_hardware_sync_config.cmake`
- Modify: `mipi_vse_bpu_zerocopy/CMakeLists.txt`

- [ ] **Step 1: Add a failing static configuration test**

Create `check_hardware_sync_config.cmake` to read the sensor and VIO sources and fail unless they contain:

```cmake
if(NOT sensor MATCHES "#define SENSOR_FPS 30")
  message(FATAL_ERROR "SC132GS must run at 30 FPS")
endif()
if(NOT sensor MATCHES "\\.sensor_mode[ \t]*=[ \t]*SLAVE_M")
  message(FATAL_ERROR "SC132GS must use SLAVE_M")
endif()
if(NOT sensor MATCHES "\\.lpwm_attr[ \t\r\n]*=[ \t\r\n]*\\{[ \t\r\n]*\\.enable[ \t]*=[ \t]*1")
  message(FATAL_ERROR "SC132GS LPWM must be enabled")
endif()
if(vio MATCHES "lpwm_attr\\.enable[ \t]*=[ \t]*0")
  message(FATAL_ERROR "runtime code must not disable LPWM")
endif()
```

Register it in `CMakeLists.txt` as test `hardware_sync_config` with `PROJECT_SOURCE_DIR` passed to the script.

- [ ] **Step 2: Run the static test and verify it fails**

Run:

```bash
cmake -S mipi_vse_bpu_zerocopy -B /tmp/mipi-sync-host -DBUILD_HARDWARE_APP=OFF
ctest --test-dir /tmp/mipi-sync-host -R hardware_sync_config --output-on-failure
```

Expected: FAIL because FPS is 60, the sensor is normal mode, and runtime disables LPWM.

- [ ] **Step 3: Apply the official SC132GS trigger configuration**

Change the sensor source to:

```c
#define SENSOR_FPS 30

static camera_config_t camera_config = {
    .name = "sc132gs",
    .addr = 0x33,
    .sensor_mode = SLAVE_M,
    .fps = SENSOR_FPS,
    /* existing geometry and MIPI fields remain unchanged */
};

static vin_node_attr_t vin_node_attr = {
    /* existing cim_attr remains unchanged */
    .lpwm_attr = {
        .enable = 1,
        .lpwm_chn_attr = {
            {0, 0, 33333, 10, 100, 0, 0},
            {0, 0, 33333, 10, 100, 0, 0},
            {0, 0, 33333, 10, 100, 0, 0},
            {0, 0, 33333, 10, 100, 0, 0},
        },
    },
};
```

Keep `isp_attr.sensor_mode = ISP_NORMAL_M`; external trigger slave mode is a sensor mode, not an ISP HDR mode.

- [ ] **Step 4: Stop runtime code from disabling LPWM and apply named timing fields**

In `cloneSensorConfig`, replace the disable assignment with explicit population of all four local LPWM attributes from `sc132gsHardwareSyncTiming()`:

```cpp
const auto sync = sc132gsHardwareSyncTiming();
pipeline.vin_node_attr.lpwm_attr.enable = 1;
for (auto& channel : pipeline.vin_node_attr.lpwm_attr.lpwm_chn_attr) {
  channel.trigger_source = sync.trigger_source;
  channel.trigger_mode = sync.trigger_mode;
  channel.period = sync.period_us;
  channel.offset = sync.offset_us;
  channel.duty_time = sync.duty_us;
  channel.threshold = 0;
  channel.adjust_step = 0;
}
```

- [ ] **Step 5: Run static and unit tests**

Run:

```bash
cmake --build /tmp/mipi-sync-host -j4
ctest --test-dir /tmp/mipi-sync-host --output-on-failure
```

Expected: all host tests pass, including `hardware_sync_config` and `no_image_copy`.

- [ ] **Step 6: Commit sensor synchronization configuration**

```bash
git add mipi_vse_bpu_zerocopy/CMakeLists.txt \
  mipi_vse_bpu_zerocopy/test/check_hardware_sync_config.cmake \
  mipi_vse_bpu_zerocopy/src/sensor/sc132gs_linear_1088x1280_raw10_30fps_1lane.c \
  mipi_vse_bpu_zerocopy/src/vio_pipeline.cpp
git commit -m "feat: trigger sc132gs cameras from lpwm"
```

### Task 3: Prepare both VIO flows before starting either flow

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/src/vio_pipeline.cpp`
- Modify: `mipi_vse_bpu_zerocopy/test/check_hardware_sync_config.cmake`

- [ ] **Step 1: Extend the static test to require split prepare/start functions**

Add checks that the VIO source contains both `prepareOne(Pipeline& pipeline)` and `startOne(Pipeline& pipeline)`, and that `initialize()` calls both prepares before either start.

- [ ] **Step 2: Run the test and verify it fails**

Run `ctest --test-dir /tmp/mipi-sync-host -R hardware_sync_config --output-on-failure`.

Expected: FAIL because initialization currently starts each flow inside `initializeOne`.

- [ ] **Step 3: Split VIO preparation from flow start**

Rename `initializeOne` to `prepareOne` and end it after camera attachment. Move `hbn_vflow_start` and `pipeline.flow_started = true` into:

```cpp
bool startOne(Pipeline& pipeline) {
  if (!call("hbn_vflow_start", hbn_vflow_start(pipeline.flow), pipeline.host)) {
    return false;
  }
  pipeline.flow_started = true;
  emit(LogLevel::kInfo, "pipeline_started", pipeline.channel, -1,
       "hardware-triggered flow started",
       {{"host", static_cast<int64_t>(pipeline.host)},
        {"sync_period_us", uint64_t{33333}},
        {"vse_height", int64_t{640}}, {"vse_width", int64_t{640}}});
  return true;
}
```

Change `initialize()` to this exact ordering:

```cpp
cloneSensorConfig(right_, right_host_);
cloneSensorConfig(left_, left_host_);
if (!prepareOne(right_) || !prepareOne(left_) ||
    !startOne(right_) || !startOne(left_)) {
  stop();
  return false;
}
```

- [ ] **Step 4: Run host tests and cross-compile**

Run:

```bash
ctest --test-dir /tmp/mipi-sync-host --output-on-failure
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

Expected: all tests pass and the AArch64 hardware executable links successfully.

- [ ] **Step 5: Commit two-phase VIO startup**

```bash
git add mipi_vse_bpu_zerocopy/src/vio_pipeline.cpp \
  mipi_vse_bpu_zerocopy/test/check_hardware_sync_config.cmake
git commit -m "refactor: prepare stereo vio flows before start"
```

### Task 4: Carry and report hardware timestamp skew

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/include/vse_frame.hpp`
- Modify: `mipi_vse_bpu_zerocopy/src/vse_frame.cpp`
- Modify: `mipi_vse_bpu_zerocopy/src/vio_pipeline.cpp`
- Modify: `mipi_vse_bpu_zerocopy/src/main.cpp`
- Test: `mipi_vse_bpu_zerocopy/test/test_bounded_frame_queue.cpp`

- [ ] **Step 1: Write a failing timestamp ownership test**

Construct a frame with hardware timestamp `123456`, move it through the queue, and assert `frame.hardwareTimestamp() == 123456` before release.

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
cmake --build /tmp/mipi-sync-host --target test_bounded_frame_queue -j4
```

Expected: compilation fails because the timestamp constructor argument/accessor does not exist.

- [ ] **Step 3: Preserve timestamps in `VseFrame`**

Extend the constructor with a trailing default argument to avoid changing existing call sites:

```cpp
VseFrame(std::uint64_t frame_id, BufferLayout layout,
         ReleaseFunction release, std::uint64_t hardware_timestamp = 0);
std::uint64_t hardwareTimestamp() const noexcept;
```

Add `hardware_timestamp_`, initialize it, copy it in the move constructor/assignment, and pass `image.info.timestamps` from `vio_pipeline.cpp`.

- [ ] **Step 4: Log stereo skew before enqueue**

In `main.cpp`, calculate a signed absolute delta before moving the frames, track the maximum, and log `stereo_sync_sample` every 30 pairs:

```cpp
const uint64_t left_ts = left_frame.hardwareTimestamp();
const uint64_t right_ts = right_frame.hardwareTimestamp();
const uint64_t skew = left_ts > right_ts ? left_ts - right_ts
                                         : right_ts - left_ts;
max_timestamp_skew = std::max(max_timestamp_skew, skew);
if ((dispatched % 30U) == 0U) {
  logger.log({LogLevel::kInfo, "sync", "stereo_sync_sample", "main", "",
              static_cast<int64_t>(left_frame.frameId()),
              "left/right hardware timestamp skew",
              {{"left_timestamp", left_ts}, {"right_timestamp", right_ts},
               {"skew_ticks", skew}, {"max_skew_ticks", max_timestamp_skew}}});
}
```

Do not label SDK timestamp ticks as microseconds until the SDK clock unit has been confirmed on the board.

- [ ] **Step 5: Run all host tests and cross-compile**

Run:

```bash
cmake --build /tmp/mipi-sync-host -j4
ctest --test-dir /tmp/mipi-sync-host --output-on-failure
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
```

Expected: all tests pass and cross-compilation succeeds.

- [ ] **Step 6: Commit timestamp diagnostics**

```bash
git add mipi_vse_bpu_zerocopy/include/vse_frame.hpp \
  mipi_vse_bpu_zerocopy/src/vse_frame.cpp \
  mipi_vse_bpu_zerocopy/src/vio_pipeline.cpp \
  mipi_vse_bpu_zerocopy/src/main.cpp \
  mipi_vse_bpu_zerocopy/test/test_bounded_frame_queue.cpp
git commit -m "feat: report stereo hardware timestamp skew"
```

### Task 5: Document and validate on the X5 board

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/README.md`

- [ ] **Step 1: Document the operating model and limitation**

Add a “双摄硬件同步” section stating: sensors use SC132GS `SLAVE_M`; LPWM period is 33333 µs; inference remains pipelined; `skew_ticks` is raw SDK timestamp ticks; physical FSIN routing must be verified; two independent LPWM channels may still require one trigger signal fanned out to both sensors.

- [ ] **Step 2: Deploy the verified binary**

Copy `scripts`, model, and `build_aarch64/mipi_vse_bpu_zerocopy` to the existing board project directory without overwriting unrelated board files.

- [ ] **Step 3: Run a bounded board test**

Run on `root@192.168.1.12`:

```bash
FRAMES=300 LOG_LEVEL=info ./scripts/run_on_board.sh
```

Expected: both pipelines start, 300 stereo pairs are processed, and `stereo_sync_sample` appears every 30 pairs without acquisition timeout escalation.

- [ ] **Step 4: Analyze synchronization evidence**

Extract `left_timestamp`, `right_timestamp`, `skew_ticks`, frame IDs, maximum skew, dropped frames, and timeout count from `events.jsonl`. A stable small skew supports synchronization; a constant large phase gap or one-frame gap requires oscilloscope/flash testing and possibly a shared physical trigger.

- [ ] **Step 5: Run the final regression suite**

Run:

```bash
ctest --test-dir /tmp/mipi-sync-host --output-on-failure
docker exec 3c543521b4ca bash -lc 'cd /project/mipi_vse_bpu_zerocopy && ./build.sh'
git diff --check
git status --short
```

Expected: tests and build pass, `git diff --check` is silent, and only intentional README/code changes remain.

- [ ] **Step 6: Commit documentation and board findings**

```bash
git add mipi_vse_bpu_zerocopy/README.md
git commit -m "docs: describe stereo hardware sync validation"
```
