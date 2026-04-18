# jni-tracer Python CLI

Small standard-library CLI wrapper for `android-jni-tracer`.

Run from the repository root:

```bash
PYTHONPATH=python python3 -m jni_tracer log summary logs/jni_hook.json
```

Optional editable install, preferably in a virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -e python
jni-tracer log summary logs/jni_hook.json
```

On Homebrew-managed Python, direct system installs may be blocked by PEP 668.
Use `PYTHONPATH=python ...` or a virtual environment.
In network-restricted environments, use `PYTHONPATH=python ...` unless the
virtual environment already has `setuptools` available.

## Log Commands

```bash
PYTHONPATH=python python3 -m jni_tracer log validate logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log summary logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log natives logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log calls logs/jni_hook.json --function InvokeNative
PYTHONPATH=python python3 -m jni_tracer mock template
PYTHONPATH=python python3 -m jni_tracer mock validate mock.json
PYTHONPATH=python python3 -m jni_tracer diff logs/base.json logs/plan.json
```

## Android Run

```bash
PYTHONPATH=python python3 -m jni_tracer run \
  --harness build/jni_harness_arm64_android \
  --libs-dir target/libs/arm64-v8a \
  --so libtarget.so \
  --label target_plan \
  --invoke-plan docs/generic_invoke_plan_example.json.example
```

Outputs are stored under `runs/<run_id>/`:

```text
runs/<run_id>/
  manifest.json
  summary.json
  invoke_plan.json
  logs/
    jni_hook.log
    jni_hook.json
```

## Run Store Commands

Use run ids instead of raw paths once a run has been captured:

```bash
PYTHONPATH=python python3 -m jni_tracer runs list
PYTHONPATH=python python3 -m jni_tracer runs show <run_id>
PYTHONPATH=python python3 -m jni_tracer runs summary <run_id>
PYTHONPATH=python python3 -m jni_tracer runs natives <run_id>
PYTHONPATH=python python3 -m jni_tracer runs classes <run_id>
PYTHONPATH=python python3 -m jni_tracer runs calls <run_id> --function InvokeNative
PYTHONPATH=python python3 -m jni_tracer runs diff <base_run_id> <experiment_run_id>
```

`runs calls` supports `--function`, `--class-name`, and `--method-name`
filters. These commands read from `runs/<run_id>/logs/jni_hook.json`.

## MCP Read-Only Server

The MCP server exposes the existing run store to an LLM client without
executing the harness or touching adb:

```bash
PYTHONPATH=python python3 -m jni_tracer mcp serve --runs-root runs
```

Available read-only tools:

- `list_runs`
- `get_run`
- `get_summary`
- `get_calls`
- `get_natives`
- `get_classes`
- `diff_runs`

Tool responses are JSON text payloads. Device control remains hidden in the
default MCP mode and is only exposed through the explicit opt-in execution
mode below.

## MCP Execution Opt-In

Execution tools are only exposed when the server is started with
`--allow-execute` and a configured harness/libs directory:

```bash
PYTHONPATH=python python3 -m jni_tracer mcp serve \
  --runs-root runs \
  --allow-execute \
  --harness build/jni_harness_arm64_android \
  --libs-dir target/libs/arm64-v8a \
  --allowed-so-dir target/libs/arm64-v8a \
  --timeout-sec 30
```

Additional tools in execution mode:

- `run_harness`
- `run_invoke_plan`
- `rerun_with_mock`

`validate_mock_config` is always available because it does not execute the
harness. Execution tools only accept `so_name` as a filename inside
`--libs-dir`; path traversal and absolute SO paths are rejected. Each execution
creates a normal `runs/<run_id>/` directory with `manifest.json`,
`summary.json`, logs, and archived inline `mock.json` or `invoke_plan.json`
when provided.

`rerun_with_mock` takes a `base_run_id`, validates an inline mock config,
reruns the same target SO, reuses the base run invoke plan when present, and
returns the new run plus an immediate diff against the base run.
