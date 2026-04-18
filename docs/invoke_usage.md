# Native Invoke Usage

`--invoke` calls a native method captured through `RegisterNatives` after
`JNI_OnLoad` finishes. This lets the harness move beyond initialization-only
analysis and stimulate selected native entrypoints with fake JNI values.

## Basic Form

```bash
./jni_harness_arm64_android ./libtarget.so \
  --invoke 'com/example/app/NativeBridge.processEvent(IILjava/lang/String;)Ljava/lang/String;' \
  --arg int:1 \
  --arg int:2 \
  --arg string:test
```

Arguments use explicit `type:value` pairs.

Supported argument types:

- `bool:true` / `bool:false`
- `int:123`
- `long:123`
- `float:1.25`
- `double:1.25`
- `string:text`
- `object:0x1234` / `object:null`

## Invoke Plan

`--invoke-plan` executes multiple registered native methods in order after
`JNI_OnLoad`.

```bash
./jni_harness_arm64_android ./libtarget.so \
  --invoke-plan docs/generic_invoke_plan_example.json.example
```

Plan format:

```json
[
  {
    "target": "com/example/app/NativeBridge.processEvent(IILjava/lang/String;)Ljava/lang/String;",
    "args": ["int:1", "int:2", "string:test"]
  },
  {
    "target": "com/example/app/NativeBridge.onInputEvent(IIIII)V",
    "args": ["int:0", "int:0", "int:300", "int:500", "int:0"]
  }
]
```

Each step uses the same `type:value` argument strings as single `--invoke`.
Unknown keys are ignored, so future plan metadata can be added without
breaking older harness builds.

## Current Dispatcher Coverage

The first implementation intentionally uses typed dispatchers instead of
`libffi`, keeping Android deployment simple and predictable.

Supported signatures:

- `()V`
- `()I`
- `(IIIII)V`
- `(JIFFFF)V`
- `(JIII)V`
- `(IILjava/lang/String;)Ljava/lang/String;`
- `(Ljava/lang/String;)V`
- `(Ljava/lang/String;Ljava/lang/String;)V`
- `(JIII)I`
- `(JLjava/lang/String;)I`
- `(JI)Z`
- `(JI)Ljava/lang/String;`
- `(JI)J`
- `(JI)D`

Unsupported signatures are logged as `InvokeNative` events with
`status=unsupported_signature`.

## JSON Result

Each invoke attempt emits an `InvokeNativeStart` event before the native call
and an `InvokeNative` result event after the native call:

```json
{
  "function": "InvokeNativeStart",
  "arguments": {
    "class_name": "com/example/app/NativeBridge",
    "method_name": "processEvent",
    "sig": "(IILjava/lang/String;)Ljava/lang/String;",
    "fnPtr": "0x7a439236e4",
    "sequence_step": "0"
  }
}
```

```json
{
  "function": "InvokeNative",
  "arguments": {
    "class_name": "com/example/app/NativeBridge",
    "method_name": "processEvent",
    "sig": "(IILjava/lang/String;)Ljava/lang/String;",
    "fnPtr": "0x7a439236e4",
    "status": "called",
    "return_type": "jstring",
    "return_value": "0x70010",
    "reason": "",
    "sequence_step": "0"
  }
}
```

JNI calls made by the invoked native function are logged normally, with
`caller_module` and `caller_offset` pointing back into the target SO when
address resolution succeeds.

For single `--invoke`, `sequence_step` is `-1`. For `--invoke-plan`, it is the
zero-based plan step index.

## Output Structure

The harness still writes the same two files:

- `logs/jni_hook.log`: human-readable execution trace and call statistics
- `logs/jni_hook.json`: structured call events for CLI/MCP/LLM analysis

Sequence execution does not create a separate file. It appends one
`InvokeNativeStart` and one `InvokeNative` result event per step to
`jni_hook.json`, with any JNI calls triggered by that native function appearing
between those two events.

## Known Limitations

- The receiver is currently passed as `jclass`, so this is best suited for
  static native methods.
- Some native entrypoints may require app state initialized by another method.
  In that case they may return early or do nothing unless the plan first calls
  the required setup methods.
- This is not a full ABI-level dynamic invoker. Add explicit typed dispatchers
  for new signatures as analysis requires them.
