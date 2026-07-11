# Result Visualization Saving Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional left, right and side-by-side JPEG result saving without changing the VSE-to-BPU zero-copy input path.

**Architecture:** Each inference processor samples successful results according to a deterministic interval/max policy and copies the still-owned NV12 frame into a snapshot. A shared bounded asynchronous visualization manager converts, annotates and encodes snapshots, pairs the kth left/right successful results, and writes atomic JPEG files while exposing complete statistics and failures.

**Tech Stack:** C++17, std::thread/condition_variable/filesystem, runtime-loaded TurboJPEG ABI (`dlopen`/`dlsym`), CMake/CTest, Docker aarch64 cross-build, X5 hardware validation.

---

## File map

- Modify `include/app_config.hpp`, `src/app_config.cpp`, `test/test_app_config.cpp`: visualization CLI and validation.
- Create `include/visualization_image.hpp`, `src/visualization_image.cpp`, `test/test_visualization_image.cpp`: NV12 snapshot copy, RGB conversion, annotation, font and side-by-side image composition.
- Create `include/jpeg_encoder.hpp`, `src/jpeg_encoder.cpp`, `test/test_jpeg_encoder.cpp`: encoder interface and runtime TurboJPEG loading.
- Create `include/visualization_manager.hpp`, `src/visualization_manager.cpp`, `test/test_visualization_manager.cpp`: sampling policy, bounded queue, pairing, atomic files, events and statistics.
- Modify `include/zero_copy_channel_processor.hpp`, `src/zero_copy_channel_processor.cpp`: submit optional post-inference snapshots.
- Modify `include/channel_worker.hpp`, `src/channel_worker.cpp`: preserve visualization submit/copy metrics in per-channel stats where needed.
- Modify `src/main.cpp`: lifecycle, fatal propagation and summary JSON.
- Modify `CMakeLists.txt`: sources, tests and `${CMAKE_DL_LIBS}`.
- Modify `scripts/run_on_board.sh`, `README.md`, `TECHNICAL_PERFORMANCE_REPORT.md`: operation and performance-boundary documentation.

### Task 1: Visualization configuration

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/include/app_config.hpp`
- Modify: `mipi_vse_bpu_zerocopy/src/app_config.cpp`
- Modify: `mipi_vse_bpu_zerocopy/test/test_app_config.cpp`

- [ ] **Step 1: Add failing configuration assertions**

Extend `test_app_config.cpp` to require:

```cpp
CHECK(!defaults.visualize);
CHECK(defaults.visual_dir.empty());
CHECK(defaults.visual_interval == 10);
CHECK(defaults.visual_max_frames == 100);
CHECK(defaults.visual_quality == 90);
```

Parse `--visualize --visual-dir /tmp/visual --visual-interval 3 --visual-max-frames 7 --visual-quality 85`, and reject interval/max zero, quality zero/101 and an explicitly empty directory.

- [ ] **Step 2: Run RED**

Run:

```bash
clang++ -std=c++17 -Imipi_vse_bpu_zerocopy/include \
  mipi_vse_bpu_zerocopy/test/test_app_config.cpp \
  mipi_vse_bpu_zerocopy/src/app_config.cpp -o /tmp/test_app_config_visual_red
```

Expected: compile failure because the visualization fields do not exist.

- [ ] **Step 3: Add fields and exact CLI parsing**

Add to `AppConfig`:

```cpp
bool visualize = false;
std::string visual_dir;
int visual_interval = 10;
int visual_max_frames = 100;
int visual_quality = 90;
```

Parse the five options, validate positive interval/max and quality `1..100`, and add all options to `usage()`.

- [ ] **Step 4: Run GREEN and config regression**

Run the command from Step 2 and execute `/tmp/test_app_config_visual_red`.

Expected: `test_app_config: PASS`.

- [ ] **Step 5: Commit**

```bash
git add mipi_vse_bpu_zerocopy/include/app_config.hpp \
  mipi_vse_bpu_zerocopy/src/app_config.cpp \
  mipi_vse_bpu_zerocopy/test/test_app_config.cpp
git commit -m "feat: configure result visualization"
```

### Task 2: NV12 image conversion and annotation

**Files:**
- Create: `mipi_vse_bpu_zerocopy/include/visualization_image.hpp`
- Create: `mipi_vse_bpu_zerocopy/src/visualization_image.cpp`
- Create: `mipi_vse_bpu_zerocopy/test/test_visualization_image.cpp`
- Modify: `mipi_vse_bpu_zerocopy/CMakeLists.txt`

- [ ] **Step 1: Write image-operation tests first**

Define the desired API in the test:

```cpp
struct Nv12Image { int width, height; std::vector<uint8_t> bytes; };
struct RgbImage { int width, height; std::vector<uint8_t> pixels; };

Nv12Image copyNv12Image(const BufferLayout& layout);
RgbImage nv12ToRgb(const Nv12Image& image);
void drawDetections(RgbImage& image, const std::vector<Detection>& detections);
RgbImage combineSideBySide(const RgbImage& left, const RgbImage& right);
```

Tests must cover:

- padded source strides copied row-by-row into compact NV12;
- 4×4 neutral NV12 produces equal RGB channels within one code value;
- saturated YUV test verifies channel order is RGB;
- negative/out-of-range box is clipped and draws a two-pixel border;
- identical class IDs produce identical colors;
- label rendering changes pixels above/inside the box without writing outside the image;
- combined width is the sum, height is 640, and left/right sentinel pixels land on the correct halves;
- invalid dimensions, odd NV12 size, null planes and mismatched combine heights throw `invalid_argument`.

- [ ] **Step 2: Run RED**

```bash
clang++ -std=c++17 -Imipi_vse_bpu_zerocopy/include \
  mipi_vse_bpu_zerocopy/test/test_visualization_image.cpp \
  -o /tmp/test_visualization_image_red
```

Expected: missing `visualization_image.hpp`.

- [ ] **Step 3: Implement deterministic image operations**

Use integer BT.601 limited-range conversion:

```cpp
const int c = y - 16, d = u - 128, e = v - 128;
r = clamp((298*c + 409*e + 128) >> 8);
g = clamp((298*c - 100*d - 208*e + 128) >> 8);
b = clamp((298*c + 516*d + 128) >> 8);
```

Implement a fixed 5×7 bitmap font for `0..9`, space, `-` and `.`, format labels with two score decimals, and derive box color deterministically from `class_id`. Keep every image copy in `visualization_image.cpp`, not in the DNN runner or VIO sources.

- [ ] **Step 4: Run GREEN and sanitizer-friendly boundary tests**

Compile `test_visualization_image.cpp` with `visualization_image.cpp`, run it, then run with host AddressSanitizer if supported:

```bash
clang++ -std=c++17 -fsanitize=address -fno-omit-frame-pointer \
  -Imipi_vse_bpu_zerocopy/include \
  mipi_vse_bpu_zerocopy/test/test_visualization_image.cpp \
  mipi_vse_bpu_zerocopy/src/visualization_image.cpp \
  -o /tmp/test_visualization_image && /tmp/test_visualization_image
```

Expected: `test_visualization_image: PASS`, no sanitizer error.

- [ ] **Step 5: Add source/test targets and commit**

Add `src/visualization_image.cpp` to `mipi_zerocopy_core` and the test to CTest, then commit:

```bash
git add mipi_vse_bpu_zerocopy/CMakeLists.txt \
  mipi_vse_bpu_zerocopy/include/visualization_image.hpp \
  mipi_vse_bpu_zerocopy/src/visualization_image.cpp \
  mipi_vse_bpu_zerocopy/test/test_visualization_image.cpp
git commit -m "feat: convert and annotate visualization frames"
```

### Task 3: Runtime TurboJPEG encoder

**Files:**
- Create: `mipi_vse_bpu_zerocopy/include/jpeg_encoder.hpp`
- Create: `mipi_vse_bpu_zerocopy/src/jpeg_encoder.cpp`
- Create: `mipi_vse_bpu_zerocopy/test/test_jpeg_encoder.cpp`
- Modify: `mipi_vse_bpu_zerocopy/CMakeLists.txt`

- [ ] **Step 1: Write failing encoder contract tests**

Define:

```cpp
class JpegEncoder {
 public:
  virtual ~JpegEncoder() = default;
  virtual bool encode(const RgbImage&, int quality,
                      std::vector<uint8_t>& jpeg, std::string& error) = 0;
};

class TurboJpegEncoder final : public JpegEncoder {
 public:
  explicit TurboJpegEncoder(std::string library = "libturbojpeg.so.0");
  bool initialize(std::string& error);
  bool encode(const RgbImage&, int, std::vector<uint8_t>&, std::string&) override;
};
```

Test invalid image/quality before calling the library, encode-before-initialize, and initialization with `/definitely/missing/libturbojpeg.so` returning a useful error without crashing.

- [ ] **Step 2: Run RED**

Compile the test alone; expect missing header/types.

- [ ] **Step 3: Implement the dynamic ABI wrapper**

Load `tjInitCompress`, `tjCompress2`, `tjDestroy`, `tjFree` and `tjGetErrorStr2` with exact TurboJPEG signatures. Use pixel format RGB (`TJPF_RGB=0`), 4:2:0 subsampling (`TJSAMP_420=2`) and flags 0. Copy the returned JPEG bytes into a vector before `tjFree`. The destructor destroys the compressor then closes the library.

- [ ] **Step 4: Run GREEN on the missing-library path**

Compile with `src/jpeg_encoder.cpp` and `${CMAKE_DL_LIBS}`/`-ldl` on Linux. Expected: `test_jpeg_encoder: PASS` without requiring TurboJPEG at test runtime.

- [ ] **Step 5: Cross-build and commit**

Add the source to core, link `mipi_zerocopy_core` with `${CMAKE_DL_LIBS}`, add the test, run Docker target build, then commit:

```bash
git add mipi_vse_bpu_zerocopy/CMakeLists.txt \
  mipi_vse_bpu_zerocopy/include/jpeg_encoder.hpp \
  mipi_vse_bpu_zerocopy/src/jpeg_encoder.cpp \
  mipi_vse_bpu_zerocopy/test/test_jpeg_encoder.cpp
git commit -m "feat: encode visualization JPEGs with TurboJPEG"
```

### Task 4: Asynchronous visualization manager

**Files:**
- Create: `mipi_vse_bpu_zerocopy/include/visualization_manager.hpp`
- Create: `mipi_vse_bpu_zerocopy/src/visualization_manager.cpp`
- Create: `mipi_vse_bpu_zerocopy/test/test_visualization_manager.cpp`
- Modify: `mipi_vse_bpu_zerocopy/CMakeLists.txt`

- [ ] **Step 1: Write manager behavior tests with a fake encoder**

Required public structures:

```cpp
struct VisualizationConfig {
  bool enabled = false;
  std::string directory;
  int interval = 10;
  int max_frames = 100;
  int quality = 90;
  std::size_t queue_capacity = 4;
};
struct VisualizationStats {
  uint64_t submitted = 0, left_saved = 0, right_saved = 0;
  uint64_t combined_saved = 0, errors = 0, unpaired = 0;
  double submit_wait_ms = 0, convert_ms = 0, encode_write_ms = 0;
};
```

Create a fake `JpegEncoder` that records image dimensions and emits `{0xff,0xd8,0xff,0xd9}`. Tests must prove:

- `shouldCapture(1,10,100)`, `shouldCapture(11,10,100)` are true; 2/10/1002 are false;
- disabled manager never copies/submits and creates no directory;
- left and right sequence 1 arriving in either order create two 640×640 calls and one 1280×640 call with exact filenames;
- sequences 1 and 11 produce separate pairs;
- queue capacity one blocks a producer until the writer consumes;
- shutdown drains accepted tasks and reports one unpaired snapshot;
- encoder failure makes submit/health fail, increments errors, calls the fatal callback once and leaves no final or temporary JPEG;
- successful atomic write leaves final JPEG magic and no `.tmp` file.

- [ ] **Step 2: Run RED**

Compile test with existing image/encoder sources; expect missing manager header.

- [ ] **Step 3: Implement queue, writer, pairing and atomic files**

`submit()` synchronously calls `copyNv12Image` while the VSE frame is valid, records copy/wait duration, and moves the snapshot into a bounded queue. The writer converts and annotates, writes the single image, stores the annotated RGB by success sequence, and writes combined when both channels exist. Use `std::filesystem::rename(temp, final)` only after the temporary stream closes successfully.

Provide `start()`, `shouldCapture(uint64_t)`, `submit(channel, sequence, frame_id, layout, detections, error)`, `shutdown()`, `healthy()`, `lastError()` and `stats()`.

- [ ] **Step 4: Run GREEN, all manager tests and diff check**

Compile/run the manager test with pthread and all visualization sources. Expected: PASS and no leftover temporary files.

- [ ] **Step 5: Commit**

```bash
git add mipi_vse_bpu_zerocopy/CMakeLists.txt \
  mipi_vse_bpu_zerocopy/include/visualization_manager.hpp \
  mipi_vse_bpu_zerocopy/src/visualization_manager.cpp \
  mipi_vse_bpu_zerocopy/test/test_visualization_manager.cpp
git commit -m "feat: save paired visualization results asynchronously"
```

### Task 5: Integrate inference, lifecycle, logging and summary

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/include/zero_copy_channel_processor.hpp`
- Modify: `mipi_vse_bpu_zerocopy/src/zero_copy_channel_processor.cpp`
- Modify: `mipi_vse_bpu_zerocopy/include/channel_worker.hpp`
- Modify: `mipi_vse_bpu_zerocopy/src/channel_worker.cpp`
- Modify: `mipi_vse_bpu_zerocopy/src/main.cpp`
- Modify: `mipi_vse_bpu_zerocopy/test/test_channel_worker.cpp`
- Modify: `mipi_vse_bpu_zerocopy/test/test_dual_channel_coordinator.cpp`

- [ ] **Step 1: Add failing integration/lifetime tests**

Extend injected processor/worker tests to verify a visualization callback sees `frame.valid()==true`, snapshot submit completes before release count changes, disabled visualization has zero calls, submit failure becomes the first channel error, and coordinator closes both queues. Add a lifecycle-order recorder that requires:

```text
worker_join -> processor_shutdown -> vio_stop -> visualization_shutdown -> summary
```

- [ ] **Step 2: Run RED**

Run host worker/coordinator tests. Expected: failure because visualization integration and lifecycle hook do not exist.

- [ ] **Step 3: Integrate one shared manager**

Change factory signature to accept `VisualizationManager*`. Each processor increments `success_sequence_` only after successful inference/address validation. If `shouldCapture` is true, call manager `submit` before returning; propagate its error so the worker/coordinator fatal path stops both queues.

In `main.cpp`, resolve empty visual dir to `config.log_dir + "/visual"`, start the manager before runner initialization, pass it to both processors, check health in the acquisition loop, and shutdown after VIO stop but before `summary.json`. Disabled mode uses a manager with `enabled=false` and no thread/directory.

- [ ] **Step 4: Add events and summary JSON**

Emit stable visualization events and add a top-level `visualization` object to summary with config, counts, errors, unpaired and timing totals. A visualization failure sets exit code 1 and appears in `fatal_error`.

- [ ] **Step 5: Run all host tests and Docker full build**

Run every `test_*.cpp`, Docker `./build.sh`, `check_no_image_copy.cmake`, `git diff --check` and verify no visualization copy API appears in `vio_pipeline.cpp` or `zero_copy_yolo_runner.cpp`.

- [ ] **Step 6: Commit**

```bash
git add mipi_vse_bpu_zerocopy/include/zero_copy_channel_processor.hpp \
  mipi_vse_bpu_zerocopy/src/zero_copy_channel_processor.cpp \
  mipi_vse_bpu_zerocopy/include/channel_worker.hpp \
  mipi_vse_bpu_zerocopy/src/channel_worker.cpp \
  mipi_vse_bpu_zerocopy/src/main.cpp \
  mipi_vse_bpu_zerocopy/test/test_channel_worker.cpp \
  mipi_vse_bpu_zerocopy/test/test_dual_channel_coordinator.cpp
git commit -m "feat: integrate optional result visualization"
```

### Task 6: Board script and documentation

**Files:**
- Modify: `mipi_vse_bpu_zerocopy/scripts/run_on_board.sh`
- Modify: `mipi_vse_bpu_zerocopy/README.md`
- Modify: `mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md`

- [ ] **Step 1: Add script environment controls without changing defaults**

Use a Bash array so optional arguments are omitted unless enabled:

```bash
visual_args=()
if [[ "${VISUALIZE:-0}" == "1" ]]; then
  visual_args=(--visualize
    --visual-interval "${VISUAL_INTERVAL:-10}"
    --visual-max-frames "${VISUAL_MAX_FRAMES:-100}"
    --visual-quality "${VISUAL_QUALITY:-90}")
  [[ -n "${VISUAL_DIR:-}" ]] && visual_args+=(--visual-dir "${VISUAL_DIR}")
fi
```

Pass `"${visual_args[@]}"` to the application while preserving model/frames/score/NMS/log arguments and `pipefail`.

- [ ] **Step 2: Document operation and performance impact**

README must show disabled default and an example:

```bash
VISUALIZE=1 VISUAL_INTERVAL=10 VISUAL_MAX_FRAMES=20 \
VISUAL_QUALITY=90 ./run_on_board.sh
```

Document left/right/combined directories, sampling sequence, `class_id score` labels, runtime TurboJPEG requirement, and that visualization copies NV12 after inference and changes performance.

Update the performance report metric boundary/limitations so future visualization-enabled results are explicitly identified and not compared to the existing disabled baseline.

- [ ] **Step 3: Validate shell and docs and commit**

```bash
bash -n mipi_vse_bpu_zerocopy/scripts/run_on_board.sh
git diff --check
git add mipi_vse_bpu_zerocopy/scripts/run_on_board.sh \
  mipi_vse_bpu_zerocopy/README.md \
  mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md
git commit -m "docs: describe visualization result saving"
```

### Task 7: X5 hardware acceptance

**Files:**
- Modify project files only after reproducing a failure with a regression test.

- [ ] **Step 1: Verify runtime dependency and deploy**

On `root@192.168.1.12`, verify `libturbojpeg.so.0`, deploy the new binary/script, and preserve the existing model/log directories.

- [ ] **Step 2: Validate disabled mode**

Run two frames per side without `VISUALIZE`. Assert exit zero, left/right successes two, address mismatches zero, and no visual directory under the new run directory.

- [ ] **Step 3: Validate interval/max and all three outputs**

Run:

```bash
FRAMES=12 VISUALIZE=1 VISUAL_INTERVAL=3 VISUAL_MAX_FRAMES=3 \
VISUAL_QUALITY=85 LOG_LEVEL=debug ./run_on_board.sh
```

Expected sampled success sequences 1,4,7; exactly three left JPEGs, three right JPEGs and three combined JPEGs.

- [ ] **Step 4: Validate JPEG files and annotations**

Use Python/Pillow if present, otherwise JPEG SOF parsing, to assert single dimensions 640×640, combined 1280×640, JPEG SOI/EOI markers, nonzero file sizes and left/right combined halves matching their corresponding annotated singles after JPEG-tolerant comparison. Inspect at least one saved image for visible box/label when detections exist.

- [ ] **Step 5: Validate summary and zero-copy evidence**

Parse JSONL/summary: visualization counts 3/3/3, errors/unpaired zero, BPU address matches 24/24, both core index zero, each VSE frame release once, clean shutdown and exit zero.

- [ ] **Step 6: Run fresh final verification and commit board fixes**

Run all host tests, Docker full build, static policy gate, shell syntax, report/doc checks and `git diff --check`. Commit only regression-proven board fixes with:

```bash
git commit -m "fix: validate JPEG visualization on X5"
```
