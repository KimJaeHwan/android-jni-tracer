# LLM/MCP Enhancement Plan

> `android-jni-tracer`를 `jni-analyzer`와 LLM 에이전트가 사용할 수 있는
> **구조화된 동적 분석 도구**로 강화하기 위한 실행 문서.
>
> 핵심 목표는 C 하네스의 장점을 유지하면서, 그 위에 JSON 품질 보강,
> Python CLI, 실행 이력, diff, MCP 서버를 단계적으로 얹는 것이다.

---

## 0. 결론

`jni-analyzer`의 성공은 LLM이 `android-jni-tracer`를 얼마나 잘 사용할 수 있느냐에 크게 좌우된다.

하지만 MCP 서버를 바로 붙이는 것보다 먼저 해야 할 일이 있다.

1. JSON 로그를 LLM/Analyzer가 신뢰할 수 있을 만큼 완전하게 만든다.
2. Python 라이브러리와 CLI로 기존 수동 실행 흐름을 감싼다.
3. 실행 이력과 diff를 구조화한다.
4. 그 위에 MCP read-only 도구를 먼저 붙인다.
5. 마지막으로 opt-in execution 도구를 붙인다.

즉, 강화 순서는 다음과 같다.

```text
C 하네스 JSON 보강
  -> Python models/log/mock/diff/runner
  -> CLI
  -> run store
  -> MCP read-only
  -> MCP execution opt-in
```

---

## 1. 현재 상태 평가

현재 레포는 이미 좋은 기반을 갖고 있다.

- `main.c`가 target `.so`를 `dlopen()`하고 `JNI_OnLoad`를 fake VM으로 호출한다.
- `fake_jni.c`가 class table, method table, string pool을 유지한다.
- `json_logger.c`가 `caller_address`, `caller_offset`, `caller_module`을 기록한다.
- `mock_config.c`가 `--mock` JSON으로 Java method return value를 주입한다.
- `docs/mock_config.md`에 mock 사용 시나리오가 잘 정리되어 있다.

이 기반은 `jni-analyzer`의 동적 검증 루프에 매우 적합하다.

```text
가설 생성
  -> mock config 생성
  -> tracer 재실행
  -> JSON diff
  -> 가설 확증/반증
```

다만 LLM 도구로 쓰기에는 아직 세 가지가 부족하다.

1. `Call*Method` 계열의 JSON 기록이 충분하지 않다.
2. 실행 결과를 run 단위로 관리하는 Python 계층이 없다.
3. 에이전트가 호출할 MCP 도구 인터페이스가 없다.

---

## 2. 강화 원칙

### 2.1 C 하네스는 계속 작게 유지한다

C 계층의 책임은 다음으로 제한한다.

- target SO 로드
- fake JNI 환경 제공
- JNI 호출 관측
- mock return 적용
- JSON/text 로그 출력

Python이나 MCP 때문에 C 하네스가 orchestration 로직을 품으면 안 된다.

### 2.2 JSON 로그가 원천 데이터다

`jni-analyzer`는 text log를 파싱하지 않는다.

따라서 분석에 필요한 정보는 반드시 `logs/jni_hook.json`에 구조화되어 있어야 한다.

Text log는 사람 디버깅용이고, JSON log는 도구/LLM 계약이다.

### 2.3 Python CLI는 MCP의 선행 조건이다

MCP 서버는 직접 adb나 C 하네스를 다루지 않는다.

대신 Python 라이브러리의 안정된 함수를 얇게 노출한다.

```text
MCP tool -> Python API -> CLI/runner/store -> C harness/adb/logs
```

### 2.4 실행 도구는 기본 비활성화한다

LLM이 임의로 target SO를 실행하거나 Android 기기를 조작하면 위험하다.

MCP 서버는 기본적으로 read-only 도구만 제공한다.

실행 도구는 명시적으로 다음 옵션이 켜졌을 때만 활성화한다.

```bash
jni-tracer mcp serve --allow-execute
```

추가로 target SO 경로 allowlist를 둔다.

```bash
jni-tracer mcp serve --allow-execute --allowed-so-dir ./target
```

---

## 3. Phase T0 - JSON 로그 보강

MCP보다 먼저 해야 하는 단계다.

### 3.1 목표

`jni_hook.json`만 보고도 다음 질문에 답할 수 있어야 한다.

- 어떤 Java class를 찾았는가?
- 어떤 method/field ID를 얻었는가?
- 어떤 `Call*Method`가 실제로 호출되었는가?
- 호출된 method가 resolved 되었는가?
- mock return이 적용되었는가?
- 반환값은 무엇이었는가?
- 호출 지점은 어떤 module/offset인가?

### 3.2 `Call*Method` JSON 기록 추가

현재 우선 보강해야 할 계열:

- `CallObjectMethod`, `CallObjectMethodV`, `CallObjectMethodA`
- `CallBooleanMethod`, `CallIntMethod`, `CallLongMethod` 등 primitive instance call
- `CallVoidMethod` 계열
- `CallStaticObjectMethod`
- `CallStaticBooleanMethod`, `CallStaticIntMethod`, `CallStaticLongMethod` 등 primitive static call
- `CallStaticVoidMethod` 계열
- `CallNonvirtual*Method` 계열

권장 JSON 형태:

```json
{
  "index": 12,
  "timestamp_sec": 1234,
  "timestamp_nsec": 567890000,
  "function": "CallBooleanMethod",
  "caller_address": "0x784f4c0d24",
  "caller_offset": "0x10d24",
  "caller_module": "libtarget.so",
  "arguments": {
    "env": "0x7ffc12345678",
    "obj": "0x5001",
    "methodID": "0x20000",
    "resolved": "true",
    "class_name": "com/security/RootChecker",
    "method_name": "isRooted",
    "sig": "()Z",
    "resolved_method": "com/security/RootChecker.isRooted()Z",
    "is_static": "false",
    "return_type": "boolean",
    "return_value": "false",
    "mock_applied": "true"
  }
}
```

Unresolved case:

```json
{
  "function": "CallBooleanMethod",
  "arguments": {
    "env": "0x7ffc12345678",
    "obj": "0x5001",
    "methodID": "0x20000",
    "resolved": "false",
    "return_type": "boolean",
    "return_value": "false",
    "mock_applied": "false"
  }
}
```

### 3.3 Mock 적용 정보 기록

Mock이 적용된 호출은 JSON에서 바로 구분되어야 한다.

필수 필드:

- `mock_applied`: `"true"` 또는 `"false"`
- `return_value`: 실제 stub이 반환한 값
- `return_type`: `boolean`, `int`, `long`, `object`, `string`, `void` 등

선택 필드:

- `mock_source`: mock entry matched / default stub
- `mock_config_hash`: Python 계층에서 run metadata에 저장

### 3.4 Method resolution 품질 보강

`GetMethodID` / `GetStaticMethodID`에서 생성한 method table 정보가 `Call*Method` JSON에 반드시 흘러가야 한다.

필수 resolved 필드:

- `class_name`
- `method_name`
- `sig`
- `resolved_method`
- `is_static`

### 3.5 RegisterNatives 유지

`RegisterNatives`는 이미 `methods` 배열을 raw JSON으로 기록한다.

이 구조는 유지하되, Python 모델에서 다음 필드로 정규화한다.

- `class_name`
- `nMethods`
- `methods[].name`
- `methods[].signature`
- `methods[].fnPtr`

### 3.6 T0 완료 조건

- [x] `Call*Method` 계열이 JSON에 기록된다.
- [x] resolved method 정보가 JSON에 들어간다.
- [x] mock 적용 여부와 return value가 JSON에 들어간다.
- [x] 기존 `FindClass`, `GetMethodID`, `RegisterNatives` JSON 포맷과 호환된다.
- [x] `ExceptionCheck`, `DeleteLocalRef`, string release 계열 등 실제 로그에서 빠진 JSON coverage를 보강한다.
- [x] `JSON_ANALYSIS_GUIDE.md`가 새 필드를 설명한다.

### 3.7 T0+ 구현 상태

`libtarget.so` 디바이스 테스트 기준:

- 기본 `JNI_OnLoad` run: text log 66 calls / JSON 66 calls로 일치
- `CallStaticObjectMethodV`, `CallObjectMethodV` resolved JSON 기록 확인
- `RegisterNatives`의 methods 배열 유지
- `ExceptionCheck`, `DeleteLocalRef` JSON 기록 확인

---

## 3.8 Phase T0.5 - Native Invoke

MCP/LLM이 단순 초기화 로그를 넘어서 native entrypoint를 직접 자극하려면
`RegisterNatives` 결과를 호출 가능한 registry로 보존해야 한다.

### 구현된 CLI

단일 호출:

```bash
./jni_harness_arm64_android ./libtarget.so \
  --invoke 'com/example/app/NativeBridge.processEvent(IILjava/lang/String;)Ljava/lang/String;' \
  --arg int:1 \
  --arg int:2 \
  --arg string:test
```

sequence 호출:

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
  }
]
```

### Invoke JSON

각 호출은 호출 전 `InvokeNativeStart`, 호출 후 `InvokeNative` 이벤트로 남는다.

```json
{
  "function": "InvokeNativeStart",
  "arguments": {
    "class_name": "com/example/app/NativeBridge",
    "method_name": "processEvent",
    "sig": "(IILjava/lang/String;)Ljava/lang/String;",
    "sequence_step": "1"
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
    "status": "called",
    "return_type": "jstring",
    "return_value": "0x70010",
    "sequence_step": "1"
  }
}
```

`sequence_step=-1`은 단일 `--invoke`, `0+`는 `--invoke-plan`의 step index다.

---

## 4. Phase T1 - Python 패키지와 CLI

### 4.1 목표

수동 adb/파일 조작 전에, 로컬 로그 분석과 mock 검증부터 Python으로 안정화한다.

초기 패키지 구조:

```text
python/
├── pyproject.toml
├── README.md
└── jni_tracer/
    ├── __init__.py
    ├── models.py
    ├── log.py
    ├── mock.py
    ├── diff.py
    ├── store.py
    ├── runner.py
    ├── adb.py
    ├── cli.py
    └── mcp/
        ├── __init__.py
        └── server.py
```

### 4.2 기술 스택

- Python 3.11+
- uv
- typer
- rich
- pydantic v2
- orjson
- sqlite3 stdlib
- mcp Python SDK는 Phase T4부터

### 4.3 Pydantic 모델

`models.py`:

```python
class JniCall:
    index: int
    timestamp_sec: int | None
    timestamp_nsec: int | None
    function: str
    caller_address: str | None
    caller_offset: str | None
    caller_module: str | None
    arguments: dict[str, object]

class JniLog:
    log_version: str | None
    timestamp: int | None
    calls: list[JniCall]
    total_calls: int | None

class MockMethodReturn:
    class_name: str
    method_name: str
    signature: str
    return_type: str
    value: bool | int | str | None

class RunRecord:
    run_id: str
    label: str | None
    so_path: str
    arch: str | None
    mock_hash: str | None
    log_json_path: str
    status: str
```

### 4.4 CLI 명령

먼저 read-only/log 중심 명령부터 만든다.

```bash
jni-tracer log validate logs/jni_hook.json
jni-tracer log summary logs/jni_hook.json
jni-tracer log calls logs/jni_hook.json --function CallBooleanMethod
jni-tracer log methods logs/jni_hook.json
jni-tracer log classes logs/jni_hook.json

jni-tracer mock template
jni-tracer mock validate mock.json

jni-tracer diff logs/base.json logs/mock.json
```

그 다음 실행 관련 명령을 붙인다.

```bash
jni-tracer run target/libfoo.so --label base
jni-tracer run target/libfoo.so --mock mock.json --label root_false
jni-tracer runs list
jni-tracer runs show <run_id>
jni-tracer runs calls <run_id>
jni-tracer runs diff <run_a> <run_b>
```

### 4.5 T1 완료 조건

- [ ] `jni-tracer log validate`가 JSON 스키마를 검증한다.
- [ ] `jni-tracer log summary`가 함수/클래스/메서드 통계를 출력한다.
- [ ] `jni-tracer mock validate`가 기존 mock JSON을 검증한다.
- [ ] `jni-tracer diff`가 두 JSON 로그의 차이를 구조화해서 출력한다.

---

## 5. Phase T2 - Run Store와 Diff

### 5.1 목표

LLM 에이전트는 파일 경로보다 run id를 다루는 편이 안전하다.

따라서 모든 실행을 SQLite에 저장한다.

기본 저장 위치:

```text
~/.cache/jni-tracer/runs.sqlite
```

### 5.2 DB 스키마

```sql
CREATE TABLE runs (
    run_id        TEXT PRIMARY KEY,
    label         TEXT,
    so_path       TEXT NOT NULL,
    arch          TEXT,
    mock_hash     TEXT,
    started_at    INTEGER NOT NULL,
    ended_at      INTEGER,
    total_calls   INTEGER,
    log_json_path TEXT NOT NULL,
    text_log_path TEXT,
    status        TEXT NOT NULL,
    error         TEXT
);

CREATE TABLE mock_configs (
    mock_hash     TEXT PRIMARY KEY,
    content_json  TEXT NOT NULL,
    created_at    INTEGER NOT NULL
);
```

### 5.3 Diff 결과

`diff`는 단순 파일 diff가 아니라 분석용 구조를 반환해야 한다.

권장 출력:

```json
{
  "run_a": "base",
  "run_b": "mock_root_false",
  "total_calls_delta": 4,
  "function_count_delta": [
    {"function": "CallBooleanMethod", "a": 3, "b": 3, "delta": 0},
    {"function": "FindClass", "a": 7, "b": 9, "delta": 2}
  ],
  "new_methods": [
    "com/example/SensitiveFlow.start()V"
  ],
  "missing_methods": [],
  "changed_return_values": [
    {
      "resolved_method": "com/security/RootChecker.isRooted()Z",
      "a": "true",
      "b": "false"
    }
  ],
  "sequence_similarity": 0.82
}
```

### 5.4 T2 완료 조건

- [ ] 실행 결과가 run id로 저장된다.
- [ ] mock config가 hash 기반으로 저장된다.
- [ ] 두 run의 diff를 JSON으로 출력할 수 있다.
- [ ] diff가 LLM이 읽기 쉬운 요약 필드를 포함한다.

---

## 6. Phase T3 - Runner와 ADB 자동화

### 6.1 목표

기존 README의 수동 흐름을 Python API로 감싼다.

수동 흐름:

```bash
adb push harness /data/local/tmp/
adb push target.so /data/local/tmp/
adb push mock.json /data/local/tmp/
adb shell "cd /data/local/tmp && LD_LIBRARY_PATH=. ./jni_harness_arm64_android --mock mock.json ./target.so"
adb pull /data/local/tmp/logs/ logs/
```

Python API:

```python
run = tracer.run(
    so_path="target/libfoo.so",
    arch="arm64",
    mock_path="mock.json",
    label="root_false",
)
```

### 6.2 구현 범위

- `adb.py`: `devices`, `push`, `pull`, `shell` 래퍼
- `runner.py`: local run / Android run orchestration
- `store.py`: run metadata 저장
- `mock.py`: temporary mock config 생성

### 6.3 플랫폼 구분

지원 실행 모드:

- `local-linux`: Linux/WSL에서 직접 `./build/jni_harness_x86_64 target.so`
- `android-adb`: Android device/emulator에서 adb shell 실행

macOS는 target Android ELF를 직접 실행할 수 없으므로, macOS에서는 보통 `android-adb` 모드를 쓴다.

### 6.4 T3 완료 조건

- [ ] `jni-tracer devices`가 adb 기기 목록을 보여준다.
- [ ] `jni-tracer run <so>`가 adb push/shell/pull을 자동 수행한다.
- [ ] 실행 결과가 run store에 저장된다.
- [ ] 실패 시 stderr/log/error가 구조화되어 저장된다.

---

## 7. Phase T4 - MCP Read-Only 서버

### 7.1 목표

LLM이 안전하게 기존 로그와 run store를 조회할 수 있게 한다.

기본 MCP 서버는 read-only만 제공한다.

```bash
jni-tracer mcp serve
```

### 7.2 Read-only tools

권장 도구:

```text
list_runs(limit: int = 20)
get_run(run_id: str)
get_calls(run_id: str, function: str | None = None, class_name: str | None = None, method_name: str | None = None)
get_log_summary(run_id: str)
find_methods(run_id: str, query: str | None = None)
find_classes(run_id: str, query: str | None = None)
diff_runs(run_a: str, run_b: str)
validate_log(path: str)
```

### 7.3 Tool response 원칙

MCP tool 응답은 자유 텍스트보다 JSON-friendly 구조를 우선한다.

좋은 응답:

```json
{
  "run_id": "run_20260418_001",
  "total_calls": 142,
  "top_functions": [
    {"function": "FindClass", "count": 23}
  ],
  "resolved_methods": [
    "com/security/RootChecker.isRooted()Z"
  ]
}
```

피해야 할 응답:

```text
FindClass가 많이 호출되었습니다. 아마 클래스 조회를 많이 한 것 같습니다.
```

해석은 `jni-analyzer`와 LLM이 한다. tracer MCP는 근거를 제공한다.

### 7.4 T4 완료 조건

- [ ] MCP 서버가 read-only로 실행된다.
- [ ] LLM/MCP 클라이언트가 run 목록과 calls를 조회할 수 있다.
- [ ] `diff_runs`가 구조화 diff를 반환한다.
- [ ] 실행 도구는 아직 노출하지 않는다.

---

## 8. Phase T5 - MCP Execution 도구

### 8.1 목표

`jni-analyzer`가 가설 검증을 위해 tracer를 재실행할 수 있게 한다.

단, 기본 비활성화다.

```bash
jni-tracer mcp serve --allow-execute --allowed-so-dir ./target
```

### 8.2 Execution tools

```text
run_harness(so_path, arch=None, mock_config=None, label=None, timeout=60)
invoke_native(so_path, target, args=None, mock_config=None, label=None, timeout=60)
run_invoke_plan(so_path, plan, mock_config=None, label=None, timeout=60)
create_mock_config(methods)
validate_mock_config(content)
rerun_with_mock(base_run_id, mock_overrides, label=None)
```

### 8.3 안전 규칙

- `--allow-execute` 없으면 실행 도구 등록 안 함
- `so_path`는 allowlist 경로 안에 있어야 함
- timeout 기본값 필수
- 모든 실행은 run store에 기록
- invoke plan은 감사 로그에 원문과 hash를 함께 남김
- mock content는 hash로 저장
- tool call arguments와 result를 감사 로그에 남김

### 8.4 T5 완료 조건

- [ ] `rerun_with_mock`가 base run과 mock run을 생성한다.
- [ ] `diff_runs`로 변화가 확인된다.
- [ ] 허용되지 않은 경로의 SO 실행이 거부된다.
- [ ] timeout과 실패 로그가 구조화된다.

---

## 9. jni-analyzer와의 계약

`jni-analyzer`는 tracer의 내부 C 구현을 알 필요가 없다.

계약은 네 가지다.

### 9.1 JSON 로그 스키마

`jni_hook.json`는 `jni-analyzer`의 Layer 0 입력이다.

Breaking change는 피한다.

필드 추가는 허용하되, 기존 필드 의미는 유지한다.

### 9.2 Python CLI

`jni-analyzer` 또는 사람이 fallback으로 사용할 수 있는 CLI를 제공한다.

### 9.3 Run Store

에이전트는 가능하면 파일 경로보다 run id를 참조한다.

### 9.4 MCP Tools

Phase 4 이후 `jni-analyzer`의 Dynamic Agent는 tracer MCP에 의존한다.

필수 도구:

- `get_calls`
- `get_log_summary`
- `diff_runs`
- `rerun_with_mock` (execution opt-in)

---

## 10. 우선순위 요약

### P0

- [ ] `Call*Method` JSON logging 추가
- [ ] mock 적용 여부와 return value JSON 기록
- [ ] `JSON_ANALYSIS_GUIDE.md` 업데이트

### P1

- [ ] Python `models.py`
- [ ] Python `log.py`
- [ ] `jni-tracer log validate`
- [ ] `jni-tracer log summary`
- [ ] `jni-tracer diff`

### P2

- [ ] run store SQLite
- [ ] `jni-tracer run`
- [ ] adb wrapper
- [ ] mock template/validate/generate

### P3

- [ ] MCP read-only server
- [ ] MCP execution opt-in
- [ ] `rerun_with_mock`

---

## 11. 첫 구현 세션 권장 범위

첫 세션은 MCP를 만들지 않는다.

권장 범위:

1. `fake_jni.c`의 `Call*Method` JSON logging 보강
2. mock 적용 여부와 return value 기록
3. 작은 sample/mock 로그로 JSON 필드 확인
4. `JSON_ANALYSIS_GUIDE.md`에 새 필드 설명 추가

완료 후 확인할 것:

```bash
cat logs/jni_hook.json | jq '.calls[] | select(.function | startswith("Call"))'
```

여기서 resolved method와 mock 정보가 보이면 다음 단계로 넘어간다.

---

## 12. Non-Goals

이번 강화 작업에서 하지 않는 것:

- C 하네스에 LLM 호출 기능 추가
- C 하네스에 MCP 직접 구현
- IDA/Ghidra 연동
- Android 앱 전체 빌드 자동화
- 악성코드 판정
- GUI

`android-jni-tracer`는 계속 동적 관측 도구다.
의미 해석과 판단은 `jni-analyzer`가 담당한다.

---

끝.
