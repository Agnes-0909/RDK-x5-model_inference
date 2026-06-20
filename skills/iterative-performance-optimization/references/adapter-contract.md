# Adapter Contract

This skill depends on stable project adapters instead of guessing local commands. Every target project should expose the following scripts under `scripts/perf/`.

## Required Scripts

- `scripts/perf/build.sh`
- `scripts/perf/deploy.sh`
- `scripts/perf/run.sh`
- `scripts/perf/pull_logs.sh`

## Optional Scripts

- `scripts/perf/analyze.sh`
- `scripts/perf/env.sh`

## General Rules

All adapter scripts should:

- run non-interactively
- return a deterministic exit status
- print actionable errors on failure
- avoid destructive cleanup outside their declared scope
- accept inputs from arguments or environment variables
- write outputs to predictable paths

If a script cannot complete its job, it must exit non-zero. Silent partial success is not acceptable.

## Shared Environment

Projects may choose their own exact variables, but the contract should make it easy to discover or set at least:

- target board host or IP
- target board user
- remote workspace directory
- local build directory
- local artifact path
- local output log directory
- remote output log directory
- workload selector or run mode when multiple workloads exist

If `scripts/perf/env.sh` exists, it should export or document the values expected by the other scripts.

## `build.sh`

Purpose:

- build the performance target locally
- fail clearly on compile or link errors

Inputs:

- build mode arguments, target selection, or environment variables
- optional platform selector

Outputs:

- produced artifact path
- build directory path if relevant

Behavior:

- should not prompt for user input
- should leave enough output to identify what artifact was built
- should exit non-zero on any build failure

## `deploy.sh`

Purpose:

- move the required binaries, libraries, configs, and helper files to the remote board

Inputs:

- artifact path or build output directory
- remote host metadata
- remote workspace path

Outputs:

- deployed artifact set on the remote board

Behavior:

- should use `scp` for artifact transfer
- may prepare the remote directory with `ssh` before copying
- should exit non-zero if any required file is not transferred
- should avoid deleting unrelated remote files

## `run.sh`

Purpose:

- execute the workload remotely and write logs to a known remote location

Inputs:

- remote host metadata
- remote workspace path
- workload selector, arguments, or dataset pointer if needed

Outputs:

- remote log files in a predictable location

Behavior:

- should use `ssh` for remote execution
- should fail clearly if the remote command fails
- should ensure logs are flushed or finalized before exiting
- should avoid backgrounding work without a reliable completion signal

## `pull_logs.sh`

Purpose:

- retrieve the relevant remote logs for local analysis

Inputs:

- remote host metadata
- remote log path
- local destination path

Outputs:

- local log files under a predictable directory

Behavior:

- should use `scp` to pull logs back
- should fail clearly if the expected remote logs are missing
- should not silently mix old and new runs without labeling them

## `analyze.sh`

Purpose:

- provide an optional first-pass summary of pulled logs

Inputs:

- local log directory

Outputs:

- markdown or JSON summary of timing breakdowns and likely hot spots

Behavior:

- should be optional; the skill must still operate if this script is absent
- should exit non-zero if it cannot parse the logs

## Discovery Expectations

The skill should be able to answer these questions from the adapters or their output:

- what got built
- where the remote workspace is
- where the run logs land on the board
- where the pulled logs land locally
- which command failed if the loop breaks

If those answers are not discoverable, the adapter contract is incomplete and should be fixed before optimization work continues.
