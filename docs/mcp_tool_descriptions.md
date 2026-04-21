# JNI Tracer MCP Tool Description Proposals

> Source-verified descriptions for all MCP tools in `python/jni_tracer/mcp/server.py`.
> All descriptions are written for LLM consumption — English, structured, field-level detail.
> All class/method names in examples are fictional samples, not derived from any real target.
> Do NOT apply until reviewed.

---

## FastMCP Server Instructions

**Current:**
```
"Inspect android-jni-tracer run stores and, when explicitly enabled,
run controlled Android JNI harness experiments."
```

**Proposed:**
```
You are connected to android-jni-tracer, a tool for analyzing JNI call behavior
of Android native libraries (.so files).

TOOL CATEGORIES
  Read-only (always available):
    list_runs, get_run, get_summary, get_calls, get_natives, get_classes,
    diff_runs, validate_mock_config

  Execution (requires --allow-execute flag on server startup):
    run_harness, run_invoke_plan, rerun_with_mock

RECOMMENDED ANALYSIS WORKFLOW
  1. list_runs              → discover available run IDs
  2. get_summary(run_id)    → understand scale and composition of a run
  3. get_natives(run_id)    → enumerate all RegisterNatives-registered methods
  4. get_calls(run_id, ...) → inspect actual JNI call sequences with filters
  5. diff_runs(a, b)        → compare base vs probe run (measure invoke effects)
  6. run_invoke_plan(...)   → directly call registered native methods (execution)
  7. rerun_with_mock(...)   → replay a run with intercepted Java callback values

JNI SIGNATURE CHEAT SHEET
  Format: (params)return_type
  I = int      J = long     Z = boolean   V = void
  F = float    D = double   B = byte      C = char
  Ljava/lang/String; = String object
  [B = byte array    [I = int array
  Example: (IILjava/lang/String;)Ljava/lang/String;
           → takes (int, int, String), returns String

IMPORTANT NOTES
  - fnPtr values are runtime addresses that change between executions.
    Never use fnPtr for stable identification; use class_name + method_name + signature.
  - get_natives may return fewer methods than nMethods indicates (output truncation).
    If methods array length < nMethods, use get_calls(function="RegisterNatives") to see raw data.
  - run_* tools push files to an Android device via adb. Ensure device is connected.
```

---

## Read-Only Tools

### `list_runs`

**Current:** `"List available jni-tracer runs."`

**Proposed:**
```python
"""
List all available jni-tracer runs stored in the runs directory, newest first.
Always call this first to discover valid run_id values before using other tools.

Parameters:
  limit (int, default=20): Maximum number of runs to return.

Returns: list of run manifest objects, each containing:
  - run_id (str): Unique run identifier, format: YYYYMMDD_HHMMSS_<label>
                  Use this value as run_id in all other tools.
  - label (str): Human-readable label assigned at run time
  - status (str): "ok" = completed successfully, "failed" = harness error
  - so_name (str): Target .so filename that was analyzed
  - returncode (int): Harness process exit code (0 = success)
  - duration_sec (float): Total execution time in seconds
  - timestamp (str): ISO 8601 creation time

Example output:
  [
    {
      "run_id": "20240101_120000_baseline_probe",
      "label": "baseline_probe",
      "status": "ok",
      "so_name": "libnative.so",
      "returncode": 0,
      "duration_sec": 3.24
    },
    ...
  ]

Next steps: get_summary(run_id) to understand a specific run's composition.
"""
```

---

### `get_run`

**Current:** `"Get a run manifest."`

**Proposed:**
```python
"""
Get the full manifest for a specific run. Contains execution metadata
not available in get_summary.

Parameters:
  run_id (str): Run identifier from list_runs.

Returns: manifest object containing:
  - run_id, label, status, so_name, returncode, duration_sec (see list_runs)
  - mcp_tool (str): Which MCP tool created this run ("run_harness", "run_invoke_plan", etc.)
  - invoke_plan (str|null): Path to invoke plan file if used
  - timeout_sec (int): Timeout that was applied
  - device_dir (str): Android device directory used

Use this to check whether a run included an invoke plan before calling diff_runs
or to verify execution parameters of a past run.
"""
```

---

### `get_summary`

**Current:** `"Get a run summary."`

**Proposed:**
```python
"""
Get statistical summary of a run's JNI call activity.
Call this second (after list_runs) to understand the scale and composition
of a run before diving into get_natives or get_calls.

Parameters:
  run_id (str): Run identifier from list_runs.

Returns: summary object with the following fields:

  total_calls (int):
    Total number of JNI calls recorded in this run.
    Baseline runs (JNI_OnLoad only) typically produce 50-100 calls.
    Invoke runs add InvokeNative entries on top.

  call_entries (int):
    Actual number of call objects in the log.
    Should equal total_calls; mismatch indicates a truncated or corrupted log.

  top_functions (list):
    JNI function call counts, sorted descending. Each entry:
      {"function": "GetStaticMethodID", "count": 30}
    Key signals:
      RegisterNatives count   → number of native class registrations
      InvokeNative count      → number of invoke plan steps executed (0 = no invoke)
      GetStaticMethodID count → Java static method callback dependency depth
      ExceptionCheck count    → error-handling density

  register_natives (list):
    Summary of RegisterNatives calls. Each entry:
      {"class_name": "com/example/sdk/NativeCore", "nMethods": 20, "caller_offset": "0x2a1b4c"}
    nMethods = total methods declared by the library for that class.
    Use get_natives(run_id) to see the full method list.

  invoke_results (list):
    Results of invoke plan steps (empty if no invoke plan was used). Each entry:
      {
        "class_name": "com/example/sdk/NativeCore",
        "method_name": "dispatchEvent",
        "sig": "(IILjava/lang/String;)Ljava/lang/String;",
        "status": "called",
        "sequence_step": 1
      }
    status values: "called" = executed, "skipped" = not reached, "error" = crashed

  classes (list of str):
    All Java class paths observed in this run (FindClass, RegisterNatives, etc.)
    Example: ["android/graphics/Bitmap", "com/example/sdk/NativeCore"]

Next steps:
  get_natives(run_id)                        → full registered method list
  get_calls(run_id)                          → raw JNI call sequence
  get_calls(run_id, function="InvokeNative") → invoke results only
"""
```

---

### `get_calls`

**Current:** `"Get calls from a run, optionally filtered."`

**Proposed:**
```python
"""
Return JNI call entries from a run log, with optional filters.
Without filters, returns ALL calls (potentially hundreds). Always use filters
to narrow results unless building a full call timeline.

Parameters:
  run_id (str): Run identifier from list_runs.

  function (str, optional): Filter by JNI function name. Common values:
    "RegisterNatives"    → native method registration entries only
    "InvokeNative"       → direct invoke results only (invoke plan steps)
    "GetStaticMethodID"  → Java static method lookups (callback dependencies)
    "FindClass"          → Java class lookups

  class_name (str, optional): Filter by Java class path.
    Example: "com/example/sdk/NativeCore"
    Matches the class_name field inside arguments.

  method_name (str, optional): Filter by method name.
    Example: "dispatchEvent"
    Matches method_name or name field inside arguments.

Filters are ANDed together.
Example: function="InvokeNative" + class_name="com/example/sdk/NativeCore"
returns only NativeCore invoke results.

Returns: list of call objects. Each object contains:

  index (int): Sequential call index (0-based). Indicates call order.

  function (str): JNI function name that was intercepted.

  caller_module (str): .so file that made this JNI call (e.g., "libnative.so").

  caller_offset (str): Hex offset within caller_module where the call originated.
    Example: "0x2a1b4c"
    Useful for IDA Pro cross-referencing to find the calling code.

  arguments (object): Function-specific arguments. Key shapes by function:

    RegisterNatives:
      {
        "class_name": "com/example/sdk/NativeCore",
        "nMethods": 20,
        "methods": [
          {
            "name": "dispatchEvent",
            "signature": "(IILjava/lang/String;)Ljava/lang/String;",
            "fnPtr": "0x7f3a2c10e8"
          }
        ]
      }
      NOTE: methods array may be shorter than nMethods if output was truncated.
      If truncated, some registered methods will be missing from this list.

    InvokeNative:
      {
        "class_name": "com/example/sdk/NativeCore",
        "method_name": "dispatchEvent",
        "sig": "(IILjava/lang/String;)Ljava/lang/String;",
        "fnPtr": "0x7f3a2c10e8",
        "status": "called",
        "return_type": "jstring",
        "return_value": "0x80020",       ← raw jstring pointer (runtime address, not readable)
        "return_string": "ok",           ← actual UTF-8 string content (USE THIS, not return_value)
        "invoke_args": "[int:1, int:0, string:test]",
        "sequence_step": 1,              ← position in invoke plan (0-based)
        "reason": ""
      }
      IMPORTANT: return_value is a raw memory pointer — it changes every run and is not
      human-readable. Always read return_string for the actual string the method returned.

    GetStaticMethodID:
      {
        "class_name": "com/example/util/DeviceHelper",
        "method_name": "getDeviceToken",
        "sig": "()Ljava/lang/String;"
      }

Example usage patterns:
  # See all invoke results with actual return values
  get_calls(run_id, function="InvokeNative")

  # See all interactions with a specific class
  get_calls(run_id, class_name="com/example/sdk/NativeCore")

  # Find what dispatchEvent returned
  get_calls(run_id, function="InvokeNative", method_name="dispatchEvent")

  # Find all Java callbacks the library depends on
  get_calls(run_id, function="GetStaticMethodID")
"""
```

---

### `get_natives`

**Current:** `"Get RegisterNatives entries for a run."`

**Proposed:**
```python
"""
Return all RegisterNatives entries from a run — the complete list of native methods
the library registered with the JVM. This is the primary tool for discovering
what native APIs a library exposes.

Parameters:
  run_id (str): Run identifier from list_runs.

Returns: list of registration objects, one per RegisterNatives call. Each object:

  class_name (str): Java class path that owns these native methods.
    Example: "com/example/sdk/NativeCore"

  caller_offset (str): Hex offset in the .so where RegisterNatives was called.
    Use this offset in IDA Pro to find the registration code.
    Example: "0x2a1b4c"

  nMethods (int): Total number of methods declared in the RegisterNatives call.
    If len(methods) < nMethods, the output was truncated — some methods are missing.
    In that case, use get_calls(run_id, function="RegisterNatives") to access raw data.

  methods (list): Registered native methods. Each entry:
    {
      "name": "dispatchEvent",
      "signature": "(IILjava/lang/String;)Ljava/lang/String;",
      "fnPtr": "0x7f3a2c10e8"
    }
    - name: Java method name that maps to this native implementation
    - signature: JNI type descriptor — use this to build invoke_plan args
    - fnPtr: Runtime function pointer. Changes every execution. Do NOT use as identifier.

TRUNCATION WARNING:
  The log may not capture all methods if the RegisterNatives call registered many methods.
  Always compare len(methods) against nMethods.
  If len(methods) < nMethods:
    → Some methods are hidden. Use get_calls(run_id, function="RegisterNatives")
      which returns the raw log entry and may contain additional method data.

Next steps after reviewing natives:
  Build an invoke_plan using the discovered method names and signatures,
  then call run_invoke_plan() to probe their runtime behavior.
"""
```

---

### `get_classes`

**Current:** `"Get class names observed in a run."`

**Proposed:**
```python
"""
Return all Java class paths that appeared in any JNI call during this run.
Useful for quickly mapping which Java-side classes the library interacts with.

Parameters:
  run_id (str): Run identifier from list_runs.

Returns: sorted list of Java class path strings.
  Example: [
    "android/graphics/Bitmap",
    "com/example/sdk/NativeCore",
    "com/example/sdk/ScriptBridge",
    "java/lang/ClassLoader",
    "java/lang/Thread"
  ]

Classes are extracted from FindClass calls, RegisterNatives class_name fields,
and GetStaticMethodID/GetMethodID class references.

Note: This is a high-level overview. For full interaction detail including
which methods were accessed per class, use:
  get_calls(run_id, class_name="com/example/sdk/NativeCore")
"""
```

---

### `diff_runs`

**Current:** `"Diff two runs by function counts and registered natives."`

**Proposed:**
```python
"""
Compare two runs and return the differences in JNI call counts and
registered native methods. Primary use: measure the effect of an invoke plan
by comparing a base run (no invoke) against a probe run (with invoke).

Parameters:
  run_a (str): Base/reference run ID (e.g., baseline run without invoke).
  run_b (str): Comparison run ID (e.g., probe run with invoke plan).
  Convention: run_a = before/baseline, run_b = after/probe. Delta = b - a.

Returns:

  total_calls (object):
    {"a": 66, "b": 77, "delta": 11}
    delta = number of additional JNI calls introduced by run_b.

  function_counts (list):
    Only functions where counts differ between runs. Each entry:
    {"function": "InvokeNative", "a": 0, "b": 4, "delta": 4}
    Functions with delta = 0 are excluded from this list.
    Key deltas to look for:
      InvokeNative delta      → how many invoke steps executed in run_b
      GetStringUTFChars delta → string reads triggered by invoke
      NewStringUTF delta      → new string objects created by invoke

  registered_natives_added (list):
    Methods that appeared in run_b's RegisterNatives but not run_a's.
    Format: [{"class_name": "...", "name": "...", "signature": "..."}]
    Usually empty if both runs analyze the same .so file.

  registered_natives_removed (list):
    Methods present in run_a's RegisterNatives that are absent in run_b.
    Usually empty if both runs analyze the same .so file.

Typical workflow:
  run_id_base  = baseline run (JNI_OnLoad only, no invoke plan)
  run_id_probe = invoke run (same .so + invoke plan applied)
  diff_runs(run_id_base, run_id_probe)
  → function_counts delta shows exactly what the invoke plan added
"""
```

---

### `validate_mock_config`

**Current:** `"Validate an inline mock config without running the harness."`

**Proposed:**
```python
"""
Validate a mock config object before using it in run_harness or run_invoke_plan.
Call this before any execution tool that accepts mock_config to catch format errors early.

Parameters:
  mock_config (object): The mock configuration to validate (see format below).

Mock config format:
  {
    "bool_returns": [
      {"class": "com/example/check/SafetyVerifier", "method": "isVerified", "sig": "()Z", "return": false}
    ],
    "int_returns": [
      {"class": "android/os/Build$VERSION", "method": "getSdkInt", "sig": "()I", "return": 34}
    ],
    "string_returns": [
      {"class": "android/os/Build", "method": "getModel", "sig": "()Ljava/lang/String;", "return": "Pixel 8"}
    ]
  }

  Rules:
  - Each section (bool_returns, int_returns, string_returns) is optional.
  - All four fields per entry are required: class, method, sig, return.
  - return value type must match the section type:
      bool_returns   → return must be true or false
      int_returns    → return must be an integer
      string_returns → return must be a string
  - class: full Java class path. Use $ for inner classes (e.g., "android/os/Build$VERSION").
  - sig: JNI method signature that exactly matches the real Java method.

Returns:
  {"status": "ok",      "errors": []}           → valid, safe to use in execution tools
  {"status": "invalid", "errors": ["message..."]} → fix these errors before proceeding

How mock config works at runtime:
  The harness intercepts GetStaticMethodID / GetMethodID calls for methods listed
  in mock config. When the library subsequently calls Call*Method on those methods,
  the harness returns the configured fake value instead of the real Java return.

  IMPORTANT LIMITATION: Mock only affects JNI-layer Java callbacks (the Call*Method
  interception pattern). It does NOT affect methods called purely within Dalvik/ART
  without going through the JNI bridge. If a mocked method shows delta=0 in diff_runs,
  the library likely calls it from the Java side rather than via JNI.
"""
```

---

## Execution Tools

> These tools require the MCP server to be started with `--allow-execute`.
> They push files to and run commands on a connected Android device via adb.

---

### `run_harness`

**Current:** `"Run the configured Android harness for JNI_OnLoad observation."`

**Proposed:**
```python
"""
Execute the JNI harness against a target .so file on a connected Android device.
Records all JNI calls made during JNI_OnLoad (library initialization).
This is the baseline observation run — no methods are directly invoked beyond
what the library calls internally during startup.

Parameters:
  so_name (str): Filename of the target .so inside the configured libs_dir.
    Example: "libnative.so"  (filename only, not a full path)

  label (str, optional): Human-readable tag for this run.
    Embedded in the run_id for easy identification. Example: "baseline_v2"

  mock_config (object, optional): Intercept Java callback return values during startup.
    Validate with validate_mock_config() before passing here.

  timeout_sec (int, optional): Execution timeout in seconds.
    Defaults to server-configured timeout (typically 30s).
    Increase if the library has a slow initialization sequence.

Returns:
  {
    "status": "ok" | "failed",
    "run_id": "20240101_115800_baseline",   ← save this for get_natives, get_calls, diff_runs
    "run_dir": "/local/path/to/run/dir",
    "returncode": 0,                        ← non-zero means harness error
    "duration_sec": 3.1,
    "summary": { ...same fields as get_summary output... },
    "manifest": { ...full run metadata... },
    "next_tools": ["get_run", "get_summary", "get_calls", "diff_runs"]
  }

After this call:
  get_natives(run_id)  → see what methods the library registered
  get_calls(run_id)    → see full JNI call sequence during startup

Typical use: run this first to establish a baseline, then run_invoke_plan
with the same .so to compare behavior with diff_runs.
"""
```

---

### `run_invoke_plan`

**Current:** `"Run the configured Android harness with an inline invoke plan."`

**Proposed:**
```python
"""
Execute the JNI harness with a sequence of direct native method calls after JNI_OnLoad.
Use this to actively probe registered native methods and observe their runtime behavior,
return values, and side effects beyond what JNI_OnLoad reveals on its own.

Prerequisite: Call get_natives() first to discover valid method names and signatures.

Parameters:
  so_name (str): Target .so filename inside libs_dir.
    Example: "libnative.so"

  invoke_plan (list): Ordered list of method call steps. Each step:
    {
      "class":     "com/example/sdk/NativeCore",
      "method":    "dispatchEvent",
      "signature": "(IILjava/lang/String;)Ljava/lang/String;",
      "args": [
        {"type": "int",    "value": 1},
        {"type": "int",    "value": 0},
        {"type": "string", "value": "test"}
      ]
    }

    Field details:
      class:     Java class path — must match exactly what get_natives returned
      method:    method name — must match exactly what get_natives returned
      signature: JNI signature — must match exactly what get_natives returned
      args:      ordered list of arguments matching the signature parameter types

    Supported arg types and their JNI equivalents:
      "int"    → I (Java int)
      "long"   → J (Java long)
      "bool"   → Z (Java boolean), value must be true or false
      "float"  → F (Java float)
      "double" → D (Java double)
      "string" → Ljava/lang/String; (Java String)

    Execution rules:
      - Steps run in array order.
      - If a step causes a crash or signal, all remaining steps are skipped.
      - Methods requiring an initialized context object (passed as a long/J parameter)
        must have that context set up by a prior step in the same plan.
        Passing 0 (null) as such a pointer will cause a crash — exclude those methods
        or ensure initialization happens first.

  label (str, optional): Tag for this run. Example: "probe_dispatch"

  mock_config (object, optional): Intercept Java callbacks during invoke execution.
    Validate with validate_mock_config() first.

  timeout_sec (int, optional): Override default execution timeout.

Returns:
  {
    "status": "ok",
    "run_id": "20240101_120000_probe_dispatch",
    "summary": {
      "invoke_results": [
        {
          "class_name": "com/example/sdk/NativeCore",
          "method_name": "dispatchEvent",
          "sig": "(IILjava/lang/String;)Ljava/lang/String;",
          "status": "called",
          "sequence_step": 0
        }
      ]
    },
    "next_tools": ["get_run", "get_summary", "get_calls", "diff_runs"]
  }

To read actual return values from invoke steps:
  get_calls(run_id, function="InvokeNative")
  → read the return_string field (NOT return_value, which is a raw pointer)

To measure what this invoke plan added vs a baseline run:
  diff_runs(baseline_run_id, this_run_id)
"""
```

---

### `rerun_with_mock`

**Current:** `"Rerun a base run with an inline mock config and return the diff."`

**Proposed:**
```python
"""
Replay an existing run with a mock config applied, then return the new run result
and a diff against the original. Use this to test how the native library behaves
when Java callback return values are changed — for example, bypassing a check or
substituting a device property.

This tool internally combines run_harness (or run_invoke_plan) + diff_runs in one call.

Parameters:
  base_run_id (str): The original run to replay. Must exist in the run store.
    The same .so file is reused. If the base run had an invoke plan, it is
    reused by default (controlled by reuse_invoke_plan).

  mock_config (object): Java callback interception configuration.
    Validate with validate_mock_config() first. See that tool for format details.

    Example — substitute a device model string returned by a Java callback:
    {
      "string_returns": [
        {
          "class": "android/os/Build",
          "method": "getModel",
          "sig": "()Ljava/lang/String;",
          "return": "Pixel 8"
        }
      ]
    }

  label (str, optional): Tag for the new experiment run.

  reuse_invoke_plan (bool, default=true):
    true  → reuse the invoke plan from the base run if it had one (recommended)
    false → only replay JNI_OnLoad initialization, skip any invoke steps

  timeout_sec (int, optional): Override timeout. Defaults to base run's original timeout.

Returns:
  {
    "status": "ok",
    "run_id": "20240101_121500_mock_experiment",
    "base_run_id": "20240101_120000_probe_dispatch",
    "experiment_run_id": "20240101_121500_mock_experiment",
    "reuse_invoke_plan": true,
    "diff": {
      "total_calls": {"a": 77, "b": 77, "delta": 0},
      "function_counts": [],
      "registered_natives_added": [],
      "registered_natives_removed": []
    },
    "summary": { ... },
    "next_tools": ["get_calls", "get_summary", "diff_runs", "rerun_with_mock"]
  }

Interpreting the diff result:
  delta = 0 across all functions:
    The mock had no observable effect on JNI call counts. This is expected when
    the library invokes the mocked Java method from the Java/Dalvik side rather
    than through a JNI Call*Method bridge. Mock only intercepts JNI-layer calls.

  delta > 0 in specific functions:
    The mock changed the library's control flow — a Java callback result affected
    which JNI calls were subsequently made (e.g., a check was bypassed).

  To inspect behavioral differences beyond counts:
    get_calls(experiment_run_id, function="InvokeNative")
    and compare return_string values against the base run.
"""
```

---

## Summary: Current vs Proposed Quality

| Tool | Current description | Issues | Priority |
|------|---------------------|--------|----------|
| `list_runs` | "List available jni-tracer runs." | No output fields, no next-step guidance | High |
| `get_run` | "Get a run manifest." | No field list, unclear when to use vs get_summary | Low |
| `get_summary` | "Get a run summary." | No field explanations at all | **Critical** |
| `get_calls` | "Get calls from a run, optionally filtered." | Filter usage not explained, no field guide | **Critical** |
| `get_natives` | "Get RegisterNatives entries for a run." | Truncation warning missing, fnPtr warning missing | **Critical** |
| `get_classes` | "Get class names observed in a run." | Minimal but acceptable | Low |
| `diff_runs` | "Diff two runs by function counts..." | run_a/run_b ordering convention not explained | High |
| `validate_mock_config` | "Validate an inline mock config..." | Mock format not shown, mock mechanism not explained | High |
| `run_harness` | "Run the configured Android harness..." | No output field guide, no workflow context | High |
| `run_invoke_plan` | "Run the configured Android harness with an inline invoke plan." | invoke_plan format completely absent | **Critical** |
| `rerun_with_mock` | "Rerun a base run with an inline mock config..." | mock format missing, diff interpretation missing | High |
| FastMCP instructions | One sentence | No workflow, no signature guide, no notes | **Critical** |

---

## Key Insights from Source Review

1. **`return_string` vs `return_value` in InvokeNative**
   `get_calls(function="InvokeNative")` returns both fields:
   - `return_value`: raw jstring pointer (e.g., `"0x80020"`) — changes every run, not readable
   - `return_string`: actual UTF-8 string the method returned (e.g., `"ok"`) — use this
   No current description mentions this distinction. An LLM reading only `return_value`
   will see a memory address and incorrectly conclude the method returned a pointer.

2. **Truncation is silent in `get_natives`**
   If the RegisterNatives call registered many methods, the log may only capture a subset.
   `nMethods` reflects the declared total; `methods` array length may be smaller.
   No current description warns about this. An LLM will assume the method list is complete
   and may miss registered methods when building an invoke plan.

3. **`next_tools` field in execution tool responses**
   `run_harness`, `run_invoke_plan`, and `rerun_with_mock` already include a `next_tools`
   list in their JSON response (e.g., `["get_run", "get_summary", "get_calls", "diff_runs"]`).
   This is valuable LLM guidance — but no description mentions it exists.

4. **Mock timing limitation**
   Mock only intercepts JNI-layer Java method calls (GetStaticMethodID → Call*Method pattern).
   It has no effect on Java methods called purely within Dalvik/ART.
   Current descriptions do not explain this, leading to confusion when mock produces delta=0.

5. **`reuse_invoke_plan` defaults to `true`**
   In `rerun_with_mock`, the base run's invoke plan is automatically reused unless
   `reuse_invoke_plan=false` is passed. This is undocumented — an LLM may not realize
   the experiment run includes invoke steps inherited from the base run.
