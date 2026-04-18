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

Tool responses are JSON text payloads. Execution tools are intentionally not
exposed yet; device control remains in the CLI until an explicit opt-in MCP
execution mode is added.
