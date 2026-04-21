# Failure Recovery and Partial-Log Handling

`android-jni-tracer` is designed to preserve analysis value even when the target library
or harness execution does not exit cleanly.

This matters for LLM-driven experimentation because repeated `invoke_plan` probing can
discover real crash paths, callback recursion problems, or process-kill behavior. Those
outcomes should be treated as **analysis signals**, not just tool failures.

## What can happen

During `run_harness`, `run_invoke_plan`, or `rerun_with_mock`, the device-side process may:

- return successfully (`returncode = 0`)
- exit non-zero
- hit a timeout
- be killed by a signal-like condition as surfaced through adb shell
  - example: `returncode = 137`
  - interpreted as `termination_kind = "signal_9"`

When that happens, the monolithic `logs/jni_hook.json` file may be truncated before the
closing `]` / `}` is written.

## Kill-safe logging model

The harness now writes two structured logs:

- `logs/jni_hook.json`
  - normal JSON object with `calls: []`
  - easiest for standard post-processing
  - may become invalid if the process is terminated mid-write

- `logs/jni_hook.ndjson`
  - newline-delimited JSON
  - one event per line
  - survives abrupt termination much better
  - used as the recovery source when `jni_hook.json` is malformed

## Runner recovery behavior

The Python runner keeps going even if `jni_hook.json` cannot be parsed.

Recovery flow:

1. Pull device logs into the run directory
2. Try to parse `logs/jni_hook.json`
3. If JSON parsing fails:
   - record `log_parse_error` in `manifest.json`
   - try `logs/jni_hook.ndjson`
4. If NDJSON recovery succeeds:
   - generate `summary.json`
   - set `summary_source = "jni_hook.ndjson"`
5. Mark the run as failed if execution failed or parsing required recovery

## Manifest fields to check

These fields are important when handling unstable or fuzz-like runs:

- `status`
  - `"ok"` or `"failed"`

- `returncode`
  - adb shell exit code surfaced to the runner

- `termination_kind`
  - `null`, `"timeout"`, `"nonzero_exit"`, or `"signal_<n>"`

- `partial_log_available`
  - `true` if any structured log artifact was recovered

- `log_parse_error`
  - parsing failure from `jni_hook.json`

- `summary_source`
  - `"jni_hook.json"` or `"jni_hook.ndjson"`

- `log_json_path`
- `log_ndjson_path`

## How LLM agents should interpret this

Recommended interpretation rules:

- `status="ok"` and `summary_source="jni_hook.json"`
  - normal run

- `status="failed"` with `partial_log_available=true`
  - **not** a blind tool failure
  - treat as a target-side crash / kill / abnormal execution event
  - inspect `termination_kind`, `returncode`, and the last observed calls

- `status="failed"` with `summary_source="jni_hook.ndjson"`
  - recovered partial run
  - summary is still useful
  - claims must be scoped to observed events only
  - absence of later events does not prove absence of behavior

- `partial_log_available=false`
  - tool-level failure or total loss of structured execution evidence

## Practical guidance for experiments

When a run dies after an `invoke_plan` step:

1. Keep the run
2. Read `summary.json` even if the run failed
3. Inspect the last `InvokeNative` and `Call*Method*` events
4. Record which sequence step was reached before termination
5. Treat the crash path itself as a behavioral finding

This is especially useful for:

- callback recursion
- lifecycle edge cases
- invalid state transitions
- argument-sensitive instability
- fuzzing-like JNI exploration

## Important limitation

NDJSON recovery preserves only the events that were flushed before termination.

That means:

- recovered summaries are valid for observed events
- they are not proof that later planned steps did not run
- they are best interpreted as **lower-bound evidence**

## Recommended downstream handling

For higher-level agent systems:

- prefer reading `manifest.json` together with `summary.json`
- surface crash metadata in round-analysis prompts
- distinguish:
  - tool failure
  - target crash
  - partial-log recovery

This lets the analysis stack reason about unstable native behavior without losing the
experimental trace.
