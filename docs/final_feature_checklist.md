# android-jni-tracer Final Feature Checklist

이 문서는 현재 `android-jni-tracer`에 구현된 기능, 검증된 사용 흐름,
MCP 연동 방식, 남은 확장 지점을 한 곳에 모은 최종 점검 문서다.

## 1. 현재 결론

`android-jni-tracer`는 이제 다음 역할을 수행할 수 있다.

- Android native `.so`의 `JNI_OnLoad` 실행
- fake JNI 환경에서 JNI 호출 관측
- `RegisterNatives` 기반 Java native mapping 복원
- JSON/text 로그 생성
- mock config로 Java method return 주입
- 등록된 native entry 직접 invoke
- invoke plan sequence 실행
- Python CLI로 Android device 실행 자동화
- run store 기반 결과 관리
- run 간 diff
- FastMCP SDK 기반 MCP 서버
- read-only MCP 조회
- opt-in MCP execution
- mock 기반 재실행 실험

즉, 이 도구는 LLM/agent가 사용할 수 있는 **JNI 동적 관측 및 실험 도구**로
1차 완성 상태다.

## 2. C Harness 기능

### 2.1 기본 실행

하네스는 Android 기기에서 target `.so`를 로드하고 `JNI_OnLoad`를 호출한다.

```bash
adb shell "cd /data/local/tmp && LD_LIBRARY_PATH=. ./jni_harness_arm64_android ./libtarget.so"
```

주요 책임:

- target `.so` `dlopen`
- fake `JavaVM` / `JNIEnv` 제공
- `JNI_OnLoad` 호출
- fake JNI table을 통한 JNI call 관측
- text log / JSON log 출력

### 2.2 JSON Logging

주요 JSON 필드:

- `index`
- `timestamp_sec`
- `timestamp_nsec`
- `function`
- `caller_address`
- `caller_offset`
- `caller_module`
- `arguments`

분석 대상:

- `FindClass`
- `GetMethodID`
- `GetStaticMethodID`
- `Call*Method`
- `NewStringUTF`
- `GetStringUTFChars`
- `ReleaseStringUTFChars`
- `ExceptionCheck`
- `DeleteLocalRef`
- `RegisterNatives`
- `InvokeNativeStart`
- `InvokeNative`

### 2.3 RegisterNatives 복원

`RegisterNatives` 호출은 class와 native method table을 JSON에 남긴다.

예시 구조:

```json
{
  "function": "RegisterNatives",
  "arguments": {
    "class_name": "com/example/app/NativeBridge",
    "nMethods": "25",
    "methods": [
      {
        "name": "processEvent",
        "signature": "(IILjava/lang/String;)Ljava/lang/String;",
        "fnPtr": "0x..."
      }
    ]
  }
}
```

이 결과는 IDA/Ghidra MCP와 연결할 때도 핵심 anchor가 된다.

## 3. Mock 기능

Mock config는 특정 Java method return을 주입한다.

지원 섹션:

- `bool_returns`
- `int_returns`
- `string_returns`

예시:

```json
{
  "bool_returns": [
    {
      "class": "com/security/RootChecker",
      "method": "isRooted",
      "sig": "()Z",
      "return": false
    }
  ]
}
```

CLI:

```bash
PYTHONPATH=python python3 -m jni_tracer mock template
PYTHONPATH=python python3 -m jni_tracer mock validate mock.json
```

MCP:

- `validate_mock_config`
- `run_harness(..., mock_config=...)`
- `run_invoke_plan(..., mock_config=...)`
- `rerun_with_mock(...)`

## 4. Native Invoke 기능

### 4.1 단일 invoke

```bash
adb shell "cd /data/local/tmp && LD_LIBRARY_PATH=. ./jni_harness_arm64_android ./libtarget.so --invoke 'com/example/app/NativeBridge.suspend()I'"
```

### 4.2 invoke plan

```json
[
  {
    "target": "com/example/app/NativeBridge.configureDisplay(IIIII)V",
    "args": ["int:1080", "int:1920", "int:1080", "int:1920", "int:0"]
  },
  {
    "target": "com/example/app/NativeBridge.suspend()I",
    "args": []
  }
]
```

CLI:

```bash
PYTHONPATH=python python3 -m jni_tracer run \
  --harness build/jni_harness_arm64_android \
  --libs-dir target/libs/arm64-v8a \
  --so libtarget.so \
  --label invoke_plan \
  --invoke-plan docs/generic_invoke_plan_example.json.example
```

JSON 이벤트:

- `InvokeNativeStart`
- `InvokeNative`

## 5. Python CLI

### 5.1 Log commands

```bash
PYTHONPATH=python python3 -m jni_tracer log validate logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log summary logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log natives logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log classes logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer log calls logs/jni_hook.json --function InvokeNative
```

### 5.2 Android run

```bash
PYTHONPATH=python python3 -m jni_tracer run \
  --harness build/jni_harness_arm64_android \
  --libs-dir target/libs/arm64-v8a \
  --so libtarget.so \
  --label cli_run \
  --timeout-sec 30
```

지원 옵션:

- `--mock`
- `--invoke`
- `--arg`
- `--invoke-plan`
- `--timeout-sec`
- `--device-dir`
- `--runs-root`

### 5.3 Run store

출력 구조:

```text
runs/<run_id>/
  manifest.json
  summary.json
  mock.json
  invoke_plan.json
  logs/
    jni_hook.log
    jni_hook.json
```

조회 명령:

```bash
PYTHONPATH=python python3 -m jni_tracer runs list
PYTHONPATH=python python3 -m jni_tracer runs show <run_id>
PYTHONPATH=python python3 -m jni_tracer runs summary <run_id>
PYTHONPATH=python python3 -m jni_tracer runs natives <run_id>
PYTHONPATH=python python3 -m jni_tracer runs classes <run_id>
PYTHONPATH=python python3 -m jni_tracer runs calls <run_id> --function InvokeNative
PYTHONPATH=python python3 -m jni_tracer runs diff <base_run_id> <experiment_run_id>
```

## 6. FastMCP Server

MCP 서버는 공식 FastMCP Python SDK 기반이다.

설치:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -e python
```

주의:

- `mcp serve`에는 `mcp>=1.27.0`이 필요하다.
- 일반 CLI 분석 명령은 `PYTHONPATH=python ...` 방식으로도 실행 가능하다.
- 일부 Python 3.14 venv는 `setuptools`가 없을 수 있으므로 필요하면 먼저 설치한다.

```bash
python3 -m pip install setuptools
python3 -m pip install -e python --no-build-isolation
```

### 6.1 Read-only MCP

```bash
jni-tracer mcp serve --runs-root runs
```

Read-only tools:

- `list_runs(limit=20)`
- `get_run(run_id)`
- `get_summary(run_id)`
- `get_calls(run_id, function=None, class_name=None, method_name=None)`
- `get_natives(run_id)`
- `get_classes(run_id)`
- `diff_runs(run_a, run_b)`
- `validate_mock_config(mock_config)`

기본 모드에서는 adb나 harness 실행 도구가 노출되지 않는다.

### 6.2 Execution MCP

명시적 opt-in이 필요하다.

```bash
jni-tracer mcp serve \
  --runs-root runs \
  --allow-execute \
  --harness build/jni_harness_arm64_android \
  --libs-dir target/libs/arm64-v8a \
  --allowed-so-dir target/libs/arm64-v8a \
  --timeout-sec 30
```

추가 tools:

- `run_harness`
- `run_invoke_plan`
- `rerun_with_mock`

### 6.3 Execution safety

안전장치:

- `--allow-execute` 없으면 execution tools 미노출
- `--harness` 필수
- `--libs-dir` 필수
- `so_name`은 파일명만 허용
- 절대경로 SO 거부
- `../` traversal 거부
- target `.so`는 `--allowed-so-dir` 안에 있어야 함
- timeout 적용
- 모든 실행은 run store에 기록
- inline mock/invoke plan은 run store에 archive

## 7. MCP Client Integration

client별 자세한 설정 방법은 [mcp_setup_guide.md](./mcp_setup_guide.md)를 참고한다.

### 7.1 Generic stdio config

대부분의 MCP client는 stdio 서버를 다음 형태로 등록한다.

```json
{
  "mcpServers": {
    "jni-tracer": {
      "command": "/absolute/path/to/android-jni-tracer/.venv/bin/jni-tracer",
      "args": [
        "mcp",
        "serve",
        "--runs-root",
        "/absolute/path/to/android-jni-tracer/runs"
      ],
      "cwd": "/absolute/path/to/android-jni-tracer"
    }
  }
}
```

Execution mode:

```json
{
  "mcpServers": {
    "jni-tracer-exec": {
      "command": "/absolute/path/to/android-jni-tracer/.venv/bin/jni-tracer",
      "args": [
        "mcp",
        "serve",
        "--runs-root",
        "/absolute/path/to/android-jni-tracer/runs",
        "--allow-execute",
        "--harness",
        "/absolute/path/to/android-jni-tracer/build/jni_harness_arm64_android",
        "--libs-dir",
        "/absolute/path/to/android-jni-tracer/target/libs/arm64-v8a",
        "--allowed-so-dir",
        "/absolute/path/to/android-jni-tracer/target/libs/arm64-v8a",
        "--timeout-sec",
        "30"
      ],
      "cwd": "/absolute/path/to/android-jni-tracer"
    }
  }
}
```

권장 운영:

- 평소에는 `jni-tracer` read-only 서버 사용
- 실제 디바이스 실행이 필요할 때만 `jni-tracer-exec` 사용
- execution 서버는 신뢰 가능한 target directory로 제한

### 7.2 What I can configure

Codex가 할 수 있는 일:

- MCP server implementation 수정
- MCP server smoke test 작성/실행
- client config JSON 예시 생성
- 사용 중인 MCP client 설정 파일 경로를 알면 설정 파일 패치
- Android device가 연결되어 있으면 execution tools 테스트

Codex가 직접 알 수 없는 것:

- 사용자가 어떤 MCP client를 쓰는지
- 해당 client의 실제 설정 파일 위치
- GUI client 재시작 여부

따라서 실제 client 등록까지 진행하려면 다음 정보가 필요하다.

- MCP client 이름
- 설정 파일 경로
- read-only만 쓸지 execution도 켤지

## 8. Verification Policy

공개 문서에는 내부 분석 대상 앱의 run id, class name, method name, 문자열 결과를
기록하지 않는다. 실제 검증 결과는 로컬 `runs/` artifact 또는 비공개 보고서에서
관리한다.

공개 가능한 검증 항목:

- CLI `run`으로 Android device 실행이 완료되는지
- `summary.json`과 `jni_hook.json`이 생성되는지
- `RegisterNatives` 이벤트가 JSON에 기록되는지
- `run_invoke_plan`이 `InvokeNative` 이벤트를 생성하는지
- `rerun_with_mock`이 새 run과 diff 결과를 반환하는지
- FastMCP structured output이 반환되는지

## 9. Smoke Tests

### 9.1 CLI smoke

```bash
PYTHONPATH=python python3 -m jni_tracer log validate runs/<run_id>/logs/jni_hook.json
PYTHONPATH=python python3 -m jni_tracer runs summary <run_id>
PYTHONPATH=python python3 -m jni_tracer runs natives <run_id>
PYTHONPATH=python python3 -m jni_tracer runs diff <base_run_id> <experiment_run_id>
```

### 9.2 MCP read-only smoke

서버 실행:

```bash
jni-tracer mcp serve --runs-root runs
```

클라이언트에서 확인:

- `tools/list`
- `get_summary(run_id="<run_id>")`
- `get_natives(run_id="<run_id>")`
- `get_calls(run_id="<run_id>", function="InvokeNative")`

### 9.3 MCP execution smoke

```json
{
  "tool": "run_invoke_plan",
  "arguments": {
    "so_name": "libtarget.so",
    "label": "mcp_smoke",
    "invoke_plan": [
      {
        "target": "com/example/app/NativeBridge.suspend()I",
        "args": []
      }
    ],
    "timeout_sec": 30
  }
}
```

### 9.4 Mock rerun smoke

```json
{
  "tool": "rerun_with_mock",
  "arguments": {
    "base_run_id": "<base_run_id>",
    "label": "mock_rerun_smoke",
    "reuse_invoke_plan": true,
    "mock_config": {
      "bool_returns": [
        {
          "class": "com/example/Foo",
          "method": "bar",
          "sig": "()Z",
          "return": false
        }
      ]
    }
  }
}
```

## 10. Current Boundaries

할 수 있는 것:

- JNI boundary 관측
- Java native mapping 복원
- native entrypoint 직접 자극
- mock 기반 가설 실험
- run diff를 통한 변화 확인
- LLM/MCP client에서 안전한 조회와 opt-in 실행

아직 하지 않는 것:

- SO 내부 decompile
- IDA/Ghidra function graph 분석
- event id 의미 자동 복원
- Lua script package 정적 분석
- 악성/정상 판정
- GUI

## 11. Next Best Extension

가장 가치 있는 다음 확장은 IDA Pro MCP 연동이다.

이유:

- tracer는 `RegisterNatives`와 runtime evidence를 제공한다.
- IDA는 SO 내부 control flow, strings, xrefs, pseudocode를 제공한다.
- 둘을 연결하면 `fnPtr` / offset 기반으로 native function rename과 decompile 분석이 가능하다.

권장 루프:

```text
tracer MCP: RegisterNatives / fnPtr / invoke result 확보
  -> IDA MCP: fnPtr를 IDA function으로 매핑
  -> IDA MCP: decompile / xrefs / strings 분석
  -> analyzer: event id와 분기 가설 생성
  -> tracer MCP: invoke/mock rerun으로 검증
```

이 단계부터는 `android-jni-tracer` 자체보다 상위 `jni-analyzer`의 tool
orchestration 설계가 더 중요하다.
