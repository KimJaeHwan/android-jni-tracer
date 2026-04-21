from __future__ import annotations

from typing import Any

from ..diff import diff_logs
from ..log import filter_calls, load_log, registered_natives, summary
from ..store import list_runs, run_log_path, run_manifest, run_summary
from .execution import (
    ExecutionConfig,
    rerun_with_mock_tool,
    run_harness_tool,
    run_invoke_plan_tool,
    validate_mock_config_tool,
)


JSONDict = dict[str, Any]


def build_server(runs_root: str = "runs", execution_config: ExecutionConfig | None = None) -> Any:
    try:
        from mcp.server.fastmcp import FastMCP
    except ImportError as exc:
        raise RuntimeError(
            "mcp Python SDK is required for 'jni-tracer mcp serve'. "
            "Install the package with 'python3 -m pip install -e python' "
            "or 'python3 -m pip install mcp'."
        ) from exc

    config = execution_config or ExecutionConfig()
    mcp = FastMCP(
        "jni-tracer",
        instructions=(
            "You are connected to android-jni-tracer, a tool for analyzing JNI call behavior "
            "of Android native libraries (.so files).\n\n"
            "TOOL CATEGORIES\n"
            "  Read-only (always available):\n"
            "    list_runs, get_run, get_summary, get_calls, get_natives, get_classes,\n"
            "    diff_runs, validate_mock_config\n\n"
            "  Execution (requires --allow-execute flag on server startup):\n"
            "    run_harness, run_invoke_plan, rerun_with_mock\n\n"
            "RECOMMENDED ANALYSIS WORKFLOW\n"
            "  1. list_runs              -> discover available run IDs\n"
            "  2. get_summary(run_id)    -> understand scale and composition of a run\n"
            "  3. get_natives(run_id)    -> enumerate all RegisterNatives-registered methods\n"
            "  4. get_calls(run_id, ...) -> inspect actual JNI call sequences with filters\n"
            "  5. diff_runs(a, b)        -> compare base vs probe run (measure invoke effects)\n"
            "  6. run_invoke_plan(...)   -> directly call registered native methods (execution)\n"
            "  7. rerun_with_mock(...)   -> replay a run with intercepted Java callback values\n\n"
            "JNI SIGNATURE CHEAT SHEET\n"
            "  Format: (params)return_type\n"
            "  I = int      J = long     Z = boolean   V = void\n"
            "  F = float    D = double   B = byte      C = char\n"
            "  Ljava/lang/String; = String object\n"
            "  [B = byte array    [I = int array\n"
            "  Example: (IILjava/lang/String;)Ljava/lang/String;\n"
            "           -> takes (int, int, String), returns String\n\n"
            "IMPORTANT NOTES\n"
            "  - fnPtr values are runtime addresses that change between executions.\n"
            "    Never use fnPtr for stable identification; use class_name + method_name + signature.\n"
            "  - get_natives may return fewer methods than nMethods indicates (output truncation).\n"
            "    If methods array length < nMethods, use get_calls(function=\"RegisterNatives\") to see raw data.\n"
            "  - run_* tools push files to an Android device via adb. Ensure device is connected.\n\n"
            "HARNESS ARCHITECTURE\n"
            "  jni-tracer loads the target .so in a fake JNI environment with no real\n"
            "  Android runtime, no DEX execution, and no real Java classes.\n"
            "  The native library runs in isolation -- only JNI-layer calls are observable.\n\n"
            "  OBSERVATION BOUNDARY:\n"
            "    Observable:  FindClass, GetStaticMethodID, RegisterNatives,\n"
            "                 InvokeNative, Call*Method (which Java method was requested)\n"
            "    NOT visible: What that Java method actually does (requires DEX trace or IDA Pro)\n\n"
            "  Call*Method is the deepest observation point.\n"
            "  The harness intercepts the call and returns a mock value or null --\n"
            "  no real Java code executes beyond this boundary.\n\n"
            "FAKE HANDLE SYSTEM -- DO NOT ANALYZE HANDLE VALUES\n"
            "  All jclass, jmethodID, jstring, jfieldID, and jobject values in the log are\n"
            "  harness-internal fake handles, NOT real Android runtime addresses.\n"
            "  They are assigned sequentially from fixed base addresses:\n"
            "    jclass    starts at 0x10000 (increments +0x1000 per unique class)\n"
            "    jmethodID starts at 0x20000 (increments +0x10   per unique method)\n"
            "    jstring   starts at 0x70000 (increments +0x10   per unique string)\n"
            "  These ranges are intentionally non-overlapping so that the JNI call chain\n"
            "  FindClass -> GetMethodID -> Call*Method resolves correctly inside the harness.\n"
            "  The same class name always gets the same jclass handle within one run,\n"
            "  enabling the library to pass that handle to GetMethodID and have it resolved.\n"
            "  DO NOT treat these numeric handle values as meaningful analysis data.\n"
            "  Only the resolved class_name, method_name, and signature fields carry real information."
        ),
    )

    @mcp.tool(name="list_runs")
    def list_runs_tool(limit: int = 20) -> list[JSONDict]:
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
        return list_runs(runs_root)[:limit]

    @mcp.tool()
    def get_run(run_id: str) -> JSONDict:
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
        return run_manifest(runs_root, run_id)

    @mcp.tool()
    def get_summary(run_id: str) -> JSONDict:
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
              RegisterNatives count   -> number of native class registrations
              InvokeNative count      -> number of invoke plan steps executed (0 = no invoke)
              GetStaticMethodID count -> Java static method callback dependency depth
              ExceptionCheck count    -> error-handling density
            Call*Method signals (critical after run_invoke_plan):
              Any entry starting with "Call" (e.g., CallStaticObjectMethodV, CallVoidMethodV)
              means the native code attempted to call back into Java during this run.
              These are the deepest observable points -- the boundary of what jni-tracer can see.
              After spotting Call* entries here, use get_calls(function="<exact name>")
              to see which Java class/method was targeted.

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
          get_natives(run_id)                        -> full registered method list
          get_calls(run_id)                          -> raw JNI call sequence
          get_calls(run_id, function="InvokeNative") -> invoke results only
        """
        return run_summary(runs_root, run_id)

    @mcp.tool()
    def get_calls(
        run_id: str,
        function: str | None = None,
        class_name: str | None = None,
        method_name: str | None = None,
    ) -> list[JSONDict]:
        """
        Return JNI call entries from a run log, with optional filters.
        Without filters, returns ALL calls (potentially hundreds). Always use filters
        to narrow results unless building a full call timeline.

        Parameters:
          run_id (str): Run identifier from list_runs.

          function (str, optional): Filter by JNI function name. Common values:
            "RegisterNatives"    -> native method registration entries only
            "InvokeNative"       -> direct invoke results only (invoke plan steps)
            "GetStaticMethodID"  -> Java static method lookups (callback dependencies)
            "FindClass"          -> Java class lookups

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

            NOTE ON HANDLE VALUES: Any jclass, jmethodID, jstring, or jobject values
            appearing in arguments (e.g., "jclass": "0x11000", "methodID": "0x20010")
            are harness-internal fake handles, NOT real Android runtime addresses.
            They are sequential counters assigned by the fake JNI environment to maintain
            call chain continuity (FindClass -> GetMethodID -> Call*Method).
            Do NOT analyze or draw conclusions from these numeric values.
            The meaningful fields are always the resolved string names: class_name,
            method_name, signature, and return_string.

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
                "return_value": "0x80020",       <- raw jstring pointer (runtime address, not readable)
                "return_string": "ok",           <- actual UTF-8 string content (USE THIS, not return_value)
                "invoke_args": "[int:1, int:0, string:test]",
                "sequence_step": 1,              <- position in invoke plan (0-based)
                "reason": ""
              }
              IMPORTANT: return_value is a raw memory pointer -- it changes every run and is not
              human-readable. Always read return_string for the actual string the method returned.

              MOCK WARNING: If this run was executed with a mock_config (check the run manifest),
              return_string reflects the library's response to artificially injected Java callback
              values -- NOT its real production behavior. Do NOT draw behavioral conclusions from
              return values of a mocked run. Use mock runs only to observe control-flow differences
              (e.g., which JNI calls are made), not to interpret what the method "really" returns.

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
        data = load_log(run_log_path(runs_root, run_id))
        return filter_calls(data, function=function, class_name=class_name, method_name=method_name)

    @mcp.tool()
    def get_natives(run_id: str) -> list[JSONDict]:
        """
        Return all RegisterNatives entries from a run -- the complete list of native methods
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
            If len(methods) < nMethods, the output was truncated -- some methods are missing.
            In that case, use get_calls(run_id, function="RegisterNatives") to access raw data.

          methods (list): Registered native methods. Each entry:
            {
              "name": "dispatchEvent",
              "signature": "(IILjava/lang/String;)Ljava/lang/String;",
              "fnPtr": "0x7f3a2c10e8"
            }
            - name: Java method name that maps to this native implementation
            - signature: JNI type descriptor -- use this to build invoke_plan args
            - fnPtr: Runtime function pointer. Changes every execution. Do NOT use as identifier.

        TRUNCATION WARNING:
          The log may not capture all methods if the RegisterNatives call registered many methods.
          Always compare len(methods) against nMethods.
          If len(methods) < nMethods:
            -> Some methods are hidden. Use get_calls(run_id, function="RegisterNatives")
               which returns the raw log entry and may contain additional method data.

        Next steps after reviewing natives:
          Build an invoke_plan using the discovered method names and signatures,
          then call run_invoke_plan() to probe their runtime behavior.
        """
        return registered_natives(load_log(run_log_path(runs_root, run_id)))

    @mcp.tool()
    def get_classes(run_id: str) -> list[str]:
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
        return summary(load_log(run_log_path(runs_root, run_id)))["classes"]

    @mcp.tool()
    def diff_runs(run_a: str, run_b: str) -> JSONDict:
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
              InvokeNative delta      -> how many invoke steps executed in run_b
              GetStringUTFChars delta -> string reads triggered by invoke
              NewStringUTF delta      -> new string objects created by invoke

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
          -> function_counts delta shows exactly what the invoke plan added

        PREREQUISITE: diff_runs is only meaningful when comparing two runs with
        different conditions (baseline vs invoke, or no-mock vs mock).
        If you only have one run, run_invoke_plan first to create a probe run,
        then use diff_runs to measure the delta.
        """
        a = load_log(run_log_path(runs_root, run_a))
        b = load_log(run_log_path(runs_root, run_b))
        return diff_logs(a, b)

    @mcp.tool()
    def validate_mock_config(mock_config: JSONDict) -> JSONDict:
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
              bool_returns   -> return must be true or false
              int_returns    -> return must be an integer
              string_returns -> return must be a string
          - class: full Java class path. Use $ for inner classes (e.g., "android/os/Build$VERSION").
          - sig: JNI method signature that exactly matches the real Java method.

        Returns:
          {"status": "ok",      "errors": []}           -> valid, safe to use in execution tools
          {"status": "invalid", "errors": ["message..."]} -> fix these errors before proceeding

        How mock config works at runtime:
          The harness intercepts GetStaticMethodID / GetMethodID calls for methods listed
          in mock config. When the library subsequently calls Call*Method on those methods,
          the harness returns the configured fake value instead of the real Java return.

          IMPORTANT LIMITATION: Mock only affects JNI-layer Java callbacks (the Call*Method
          interception pattern). It does NOT affect methods called purely within Dalvik/ART
          without going through the JNI bridge. If a mocked method shows delta=0 in diff_runs,
          the library likely calls it from the Java side rather than via JNI.
        """
        return validate_mock_config_tool({"mock_config": mock_config})

    if config.allow_execute:

        @mcp.tool()
        def run_harness(
            so_name: str,
            label: str | None = None,
            mock_config: JSONDict | None = None,
            timeout_sec: int | None = None,
        ) -> JSONDict:
            """
            Execute the JNI harness against a target .so file on a connected Android device.
            Records all JNI calls made during JNI_OnLoad (library initialization).
            This is the baseline observation run -- no methods are directly invoked beyond
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
                "run_id": "20240101_115800_baseline",   <- save this for get_natives, get_calls, diff_runs
                "run_dir": "/local/path/to/run/dir",
                "returncode": 0,                        <- non-zero means harness error
                "duration_sec": 3.1,
                "summary": { ...same fields as get_summary output... },
                "manifest": { ...full run metadata... },
                "next_tools": ["get_run", "get_summary", "get_calls", "diff_runs"]
              }

            After this call:
              get_natives(run_id)  -> see what methods the library registered
              get_calls(run_id)    -> see full JNI call sequence during startup

            Typical use: run this first to establish a baseline, then run_invoke_plan
            with the same .so to compare behavior with diff_runs.
            """
            return run_harness_tool(
                config,
                runs_root,
                {
                    "so_name": so_name,
                    "label": label,
                    "mock_config": mock_config,
                    "timeout_sec": timeout_sec,
                },
            )

        @mcp.tool()
        def run_invoke_plan(
            so_name: str,
            invoke_plan: list[JSONDict],
            label: str | None = None,
            mock_config: JSONDict | None = None,
            timeout_sec: int | None = None,
        ) -> JSONDict:
            """
            Execute the JNI harness with a sequence of direct native method calls after JNI_OnLoad.
            Use this to actively probe registered native methods and observe their runtime behavior,
            return values, and side effects beyond what JNI_OnLoad reveals on its own.

            Prerequisite: Call get_natives() first to discover valid method names and signatures.

            WHEN TO USE:
              After get_natives reveals suspicious methods -- input injection (tap, sendKeyEvent,
              onTouchEvent), screen capture (obtainImage*, snapShotJava), event dispatch
              (sendEvent, pluginEvent), or any method whose runtime behavior is unknown --
              do NOT stop at passive observation. Actively invoke those methods to confirm
              they execute and observe their actual behavior. Passive observation of
              RegisterNatives alone cannot confirm runtime behavior.

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
                  class:     Java class path -- must match exactly what get_natives returned
                  method:    method name -- must match exactly what get_natives returned
                  signature: JNI signature -- must match exactly what get_natives returned
                  args:      ordered list of arguments matching the signature parameter types

                Supported arg types and their JNI equivalents:
                  "int"    -> I (Java int)
                  "long"   -> J (Java long)
                  "bool"   -> Z (Java boolean), value must be true or false
                  "float"  -> F (Java float)
                  "double" -> D (Java double)
                  "string" -> Ljava/lang/String; (Java String)

                Execution rules:
                  - Steps run in array order.
                  - If a step causes a crash or signal, all remaining steps are skipped.
                  - Methods requiring an initialized context object (passed as a long/J parameter)
                    must have that context set up by a prior step in the same plan.
                    Passing 0 (null) as such a pointer will cause a crash -- exclude those methods
                    or ensure initialization happens first.

              label (str, optional): Tag for this run. Example: "probe_dispatch"

              mock_config (object, optional): Intercept Java callbacks during invoke execution.
                Validate with validate_mock_config() first.
                WARNING: When mock_config is provided, Java callback return values are artificial.
                The native method return values recorded in this run reflect responses to fake
                inputs and must NOT be used as ground-truth behavioral data. Use the resulting
                run only for control-flow diff analysis (via diff_runs), not for value analysis.

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
              -> read the return_string field (NOT return_value, which is a raw pointer)

            To measure what this invoke plan added vs a baseline run:
              diff_runs(baseline_run_id, this_run_id)

            CALL*METHOD MONITORING (deepest observable layer):
              After run_invoke_plan completes, always check for Java callback attempts:
              1. get_summary(run_id) -> inspect top_functions for any entry starting with "Call"
                 Example: {"function": "CallStaticObjectMethodV", "count": 1}
              2. For each Call* entry found, run:
                 get_calls(run_id, function="<exact name from top_functions>")
                 -> reveals class_name and method_name the native tried to call back into Java
              This is the observation boundary -- you can see WHAT Java method was requested,
              but not what it does (no real Java executes in the harness).
              To trace beyond this boundary, use DEX tracing or IDA Pro.
            """
            return run_invoke_plan_tool(
                config,
                runs_root,
                {
                    "so_name": so_name,
                    "label": label,
                    "invoke_plan": invoke_plan,
                    "mock_config": mock_config,
                    "timeout_sec": timeout_sec,
                },
            )

        @mcp.tool()
        def rerun_with_mock(
            base_run_id: str,
            mock_config: JSONDict,
            label: str | None = None,
            reuse_invoke_plan: bool = True,
            timeout_sec: int | None = None,
        ) -> JSONDict:
            """
            Replay an existing run with a mock config applied, then return the new run result
            and a diff against the original. Use this to test how the native library behaves
            when Java callback return values are changed -- for example, bypassing a check or
            substituting a device property.

            This tool internally combines run_harness (or run_invoke_plan) + diff_runs in one call.

            Parameters:
              base_run_id (str): The original run to replay. Must exist in the run store.
                The same .so file is reused. If the base run had an invoke plan, it is
                reused by default (controlled by reuse_invoke_plan).

              mock_config (object): Java callback interception configuration.
                Validate with validate_mock_config() first. See that tool for format details.

                Example -- substitute a device model string returned by a Java callback:
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
                true  -> reuse the invoke plan from the base run if it had one (recommended)
                false -> only replay JNI_OnLoad initialization, skip any invoke steps

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
                The mock changed the library's control flow -- a Java callback result affected
                which JNI calls were subsequently made (e.g., a check was bypassed).

              To inspect behavioral differences beyond counts:
                get_calls(experiment_run_id, function="InvokeNative")
                and compare return_string values against the base run.

            CRITICAL -- DO NOT MISINTERPRET MOCK RUN RETURN VALUES:
              The experiment run was executed with artificially injected Java callback values.
              Any InvokeNative return_string values in the experiment run are responses to
              fake inputs, NOT the library's real production behavior.
              - Use mock runs ONLY to observe changes in JNI call patterns (diff result).
              - Do NOT treat experiment run return values as meaningful analysis findings.
              - For ground-truth return value analysis, use a non-mocked run (run_harness
                or run_invoke_plan without mock_config).
            """
            return rerun_with_mock_tool(
                config,
                runs_root,
                {
                    "base_run_id": base_run_id,
                    "label": label,
                    "mock_config": mock_config,
                    "reuse_invoke_plan": reuse_invoke_plan,
                    "timeout_sec": timeout_sec,
                },
            )

    return mcp


def serve(runs_root: str = "runs", execution_config: ExecutionConfig | None = None) -> None:
    build_server(runs_root, execution_config).run(transport="stdio")
