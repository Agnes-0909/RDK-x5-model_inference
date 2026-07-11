# Engineering Performance Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate a reproducible, engineering-oriented technical and performance report for the dual-channel MIPI-VSE-BPU zero-copy pipeline.

**Architecture:** Read the existing X5 JSONL and summary artifacts without rerunning the board, compute channel-specific distributions with one deterministic Python analysis command, then write a single Markdown report in the project root. Validate every reported number against a second extraction and keep measured facts separate from inference and recommendations.

**Tech Stack:** Markdown, Python 3 standard library, JSONL/JSON, SSH, Git.

---

## File map

- Create `mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md`: final architecture, zero-copy and performance report.
- Read `/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221831/events.jsonl`: 100-frame-per-channel performance events.
- Read `/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221831/summary.json`: final counts, averages, queue waits and cache statistics.
- Read `/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221544/events.jsonl`: two-frame debug postprocess stage evidence.

### Task 1: Extract and verify performance data

**Files:**
- Read: board log files listed in the file map.
- Create: `/tmp/mipi_vse_performance_metrics.json` as a disposable calculated artifact.

- [ ] **Step 1: Extract the two board runs to local temporary files**

Run:

```bash
mkdir -p /tmp/mipi_vse_report_data/run_100 /tmp/mipi_vse_report_data/run_debug
SSHPASS=root sshpass -e scp -o StrictHostKeyChecking=no \
  root@192.168.1.12:/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221831/{events.jsonl,summary.json} \
  /tmp/mipi_vse_report_data/run_100/
SSHPASS=root sshpass -e scp -o StrictHostKeyChecking=no \
  root@192.168.1.12:/root/mipi_vse_bpu_zerocopy/logs/run_20260711_221544/events.jsonl \
  /tmp/mipi_vse_report_data/run_debug/
```

Expected: three readable local files and no board-side changes.

- [ ] **Step 2: Calculate deterministic statistics**

Run a Python 3 standard-library script that:

```python
import json
import math
from pathlib import Path

def percentile(values, probability):
    ordered = sorted(values)
    position = (len(ordered) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)

def describe(values):
    return {
        "count": len(values),
        "min": min(values),
        "mean": sum(values) / len(values),
        "p50": percentile(values, 0.50),
        "p90": percentile(values, 0.90),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": max(values),
    }
```

Collect `total_ms` from `dnn.inference_complete`, and `bpu_ms`, `postprocess_ms`, `end_to_end_ms` from `worker.frame_processed`, grouped by `channel`. Calculate elapsed seconds from the first through last processed event, aggregate inference FPS, paired FPS, address mismatches, core mismatches, cache hit rate, errors and debug postprocess stage descriptions. Save the complete result to `/tmp/mipi_vse_performance_metrics.json`.

Expected: 100 inference and processed samples for each channel, 200 address matches, core index always zero, cache misses three per channel, and exit code zero.

- [ ] **Step 3: Cross-check calculated means against summary.json**

Run:

```bash
python3 - <<'PY'
import json
m = json.load(open('/tmp/mipi_vse_performance_metrics.json'))
s = json.load(open('/tmp/mipi_vse_report_data/run_100/summary.json'))
for channel in ('left', 'right'):
    assert abs(m[channel]['bpu_ms']['mean'] - s[channel]['avg_bpu_ms']) < 0.001
    assert abs(m[channel]['postprocess_ms']['mean'] - s[channel]['avg_postprocess_ms']) < 0.001
    assert abs(m[channel]['end_to_end_ms']['mean'] - s[channel]['avg_end_to_end_ms']) < 0.001
print('PERFORMANCE_CROSS_CHECK=PASS')
PY
```

Expected: `PERFORMANCE_CROSS_CHECK=PASS`.

### Task 2: Write and validate the engineering report

**Files:**
- Create: `mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md`.

- [ ] **Step 1: Write the report from the approved design and calculated JSON**

Create the report with these exact top-level sections:

```markdown
# MIPI-VSE-BPU 双路零拷贝技术与性能报告
## 1. 报告摘要
## 2. 测试对象与环境
## 3. 系统链路与并发架构
## 4. 零拷贝实现与证据
## 5. 耗时指标定义
## 6. 测试方法与数据口径
## 7. 100 帧性能结果
## 8. 后处理耗时构成
## 9. 性能瓶颈分析
## 10. 优化建议
## 11. 复现与诊断命令
## 12. 结论与限制
```

Include exact calculated tables, the single-core scheduling interpretation, resource lifetime, address/cache proof, queue backpressure, metric boundary warnings and commands that reproduce the data.

- [ ] **Step 2: Validate report completeness and number provenance**

Run:

```bash
rg -n '^## ' mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md
rg -n 'TBD|TODO|待补充|占位' mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md && exit 1 || true
git diff --check
```

Expected: all 12 sections are present, placeholder search has no match, and `git diff --check` exits zero.

- [ ] **Step 3: Commit the report**

```bash
git add mipi_vse_bpu_zerocopy/TECHNICAL_PERFORMANCE_REPORT.md
git commit -m "docs: add engineering performance report"
```

Expected: one documentation-only commit with the report in the requested project folder.
