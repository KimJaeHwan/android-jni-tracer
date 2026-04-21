# JNI Tracer MCP 도구 설명서 (한국어)

> `python/jni_tracer/mcp/server.py`의 모든 MCP 도구에 대한 소스 검증 설명.
> 모든 클래스/메서드 이름은 실제 분석 대상에서 파생되지 않은 가상의 예시입니다.
> 영문 원본: `docs/mcp_tool_descriptions.md`

---

## FastMCP 서버 Instructions

**현재:**
```
"Inspect android-jni-tracer run stores and, when explicitly enabled,
run controlled Android JNI harness experiments."
```

**적용됨:**
```
android-jni-tracer에 연결되었습니다. 이 도구는 Android 네이티브 라이브러리(.so 파일)의
JNI 호출 동작을 분석하기 위한 도구입니다.

도구 분류
  읽기 전용 (항상 사용 가능):
    list_runs, get_run, get_summary, get_calls, get_natives, get_classes,
    diff_runs, validate_mock_config

  실행 (서버 시작 시 --allow-execute 플래그 필요):
    run_harness, run_invoke_plan, rerun_with_mock

권장 분석 워크플로우
  1. list_runs              → 사용 가능한 run ID 확인
  2. get_summary(run_id)    → run의 규모와 구성 파악
  3. get_natives(run_id)    → RegisterNatives로 등록된 모든 메서드 목록 조회
  4. get_calls(run_id, ...) → 필터를 사용해 실제 JNI 호출 시퀀스 검사
  5. diff_runs(a, b)        → 기본 run vs 프로브 run 비교 (invoke 효과 측정)
  6. run_invoke_plan(...)   → 등록된 네이티브 메서드 직접 호출 (실행)
  7. rerun_with_mock(...)   → Java 콜백 값을 조작하여 run 재실행

JNI 시그니처 요약
  형식: (파라미터)반환타입
  I = int      J = long     Z = boolean   V = void
  F = float    D = double   B = byte      C = char
  Ljava/lang/String; = String 객체
  [B = byte 배열    [I = int 배열
  예시: (IILjava/lang/String;)Ljava/lang/String;
        → (int, int, String)을 받아서 String 반환

중요 사항
  - fnPtr 값은 실행마다 변경되는 런타임 주소입니다.
    안정적인 식별에는 fnPtr을 사용하지 말고 class_name + method_name + signature를 사용하세요.
  - get_natives는 nMethods가 나타내는 것보다 적은 메서드를 반환할 수 있습니다 (출력 잘림).
    methods 배열 길이 < nMethods인 경우, get_calls(function="RegisterNatives")로 원시 데이터를 확인하세요.
  - run_* 도구들은 adb를 통해 Android 기기에 파일을 푸시합니다. 기기가 연결되어 있는지 확인하세요.

하네스 아키텍처
  jni-tracer는 실제 Android 런타임, DEX 실행, 실제 Java 클래스 없이
  가짜 JNI 환경에서 대상 .so 파일만 단독으로 로드합니다.
  네이티브 라이브러리는 격리된 환경에서 실행 — JNI 레이어 호출만 관찰 가능합니다.

  관찰 경계:
    관찰 가능:  FindClass, GetStaticMethodID, RegisterNatives,
               InvokeNative, Call*Method (어떤 Java 메서드가 요청되었는지)
    관찰 불가:  해당 Java 메서드가 실제로 하는 일 (DEX 트레이스 또는 IDA Pro 필요)

  Call*Method가 가장 깊은 관찰 지점입니다.
  하네스는 이 호출을 가로채고 mock 값 또는 null을 반환하며,
  이 경계 너머로는 실제 Java 코드가 실행되지 않습니다.

가짜 핸들 시스템 — 핸들 값을 분석하지 마세요
  로그에 나타나는 모든 jclass, jmethodID, jstring, jfieldID, jobject 값은
  하네스 내부의 가짜 핸들이며, 실제 Android 런타임 주소가 아닙니다.
  고정 베이스 주소에서 순차적으로 할당됩니다:
    jclass    0x10000부터 시작 (새 클래스마다 +0x1000 증가)
    jmethodID 0x20000부터 시작 (새 메서드마다 +0x10 증가)
    jstring   0x70000부터 시작 (새 문자열마다 +0x10 증가)
  이 범위들은 의도적으로 겹치지 않게 설계되어
  FindClass → GetMethodID → Call*Method 체인이 하네스 내에서 올바르게 동작합니다.
  같은 클래스명은 하나의 run 내에서 항상 동일한 jclass 핸들을 받으며,
  라이브러리가 그 핸들을 GetMethodID에 전달했을 때 정상적으로 해석됩니다.
  이 숫자 핸들 값을 의미 있는 분석 데이터로 취급하지 마세요.
  실제 정보는 오직 class_name, method_name, signature, return_string 필드에만 있습니다.
```

---

## 읽기 전용 도구

### `list_runs`

**현재:** `"List available jni-tracer runs."`

**설명:**
```
runs 디렉터리에 저장된 모든 jni-tracer run을 최신순으로 나열합니다.
다른 도구를 사용하기 전에 항상 이 도구를 먼저 호출하여 유효한 run_id 값을 확인하세요.

파라미터:
  limit (int, 기본값=20): 반환할 최대 run 수.

반환값: run manifest 객체 목록, 각 항목 구성:
  - run_id (str): 고유 run 식별자, 형식: YYYYMMDD_HHMMSS_<label>
                  다른 모든 도구에서 run_id 값으로 사용하세요.
  - label (str): 실행 시 지정한 사람이 읽을 수 있는 레이블
  - status (str): "ok" = 성공적으로 완료, "failed" = harness 오류
  - so_name (str): 분석된 대상 .so 파일명
  - returncode (int): harness 프로세스 종료 코드 (0 = 성공)
  - duration_sec (float): 총 실행 시간 (초)
  - timestamp (str): ISO 8601 형식 생성 시각

예시 출력:
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

다음 단계: get_summary(run_id)로 특정 run의 구성 파악.
```

---

### `get_run`

**현재:** `"Get a run manifest."`

**설명:**
```
특정 run의 전체 manifest를 가져옵니다. get_summary에서는 제공되지 않는
실행 메타데이터를 포함합니다.

파라미터:
  run_id (str): list_runs에서 가져온 run 식별자.

반환값: manifest 객체 구성:
  - run_id, label, status, so_name, returncode, duration_sec (list_runs 참조)
  - mcp_tool (str): 이 run을 생성한 MCP 도구 ("run_harness", "run_invoke_plan" 등)
  - invoke_plan (str|null): invoke plan 파일 경로 (사용된 경우)
  - timeout_sec (int): 적용된 타임아웃
  - device_dir (str): 사용된 Android 기기 디렉터리

diff_runs 호출 전 run에 invoke plan이 포함되어 있는지 확인하거나,
과거 run의 실행 파라미터를 검증할 때 사용합니다.
```

---

### `get_summary`

**현재:** `"Get a run summary."`

**설명:**
```
run의 JNI 호출 활동 통계 요약을 가져옵니다.
get_natives나 get_calls를 사용하기 전에 두 번째로 호출하여 (list_runs 이후)
run의 규모와 구성을 파악하세요.

파라미터:
  run_id (str): list_runs에서 가져온 run 식별자.

반환값: 다음 필드를 포함하는 summary 객체:

  total_calls (int):
    이 run에서 기록된 총 JNI 호출 수.
    기본 run (JNI_OnLoad만): 보통 50-100회.
    Invoke run은 InvokeNative 항목이 추가됩니다.

  call_entries (int):
    로그의 실제 call 객체 수.
    total_calls와 같아야 함. 불일치는 잘림 또는 손상된 로그를 의미합니다.

  top_functions (list):
    JNI 함수 호출 횟수, 내림차순 정렬. 각 항목:
      {"function": "GetStaticMethodID", "count": 30}
    주요 신호:
      RegisterNatives 수      → 네이티브 클래스 등록 수
      InvokeNative 수         → invoke plan 실행 단계 수 (0 = invoke 없음)
      GetStaticMethodID 수    → Java static 메서드 콜백 의존성 깊이
      ExceptionCheck 수       → 에러 처리 밀도
    Call*Method 신호 (run_invoke_plan 이후 핵심 확인 항목):
      "Call"로 시작하는 항목 (예: CallStaticObjectMethodV, CallVoidMethodV)은
      네이티브 코드가 이 run에서 Java로 역호출을 시도했음을 의미합니다.
      이것이 jni-tracer가 볼 수 있는 가장 깊은 관찰 지점입니다.
      Call* 항목 발견 시 get_calls(function="<exact name>")으로 상세 확인하세요.

  register_natives (list):
    RegisterNatives 호출 요약. 각 항목:
      {"class_name": "com/example/sdk/NativeCore", "nMethods": 20, "caller_offset": "0x2a1b4c"}
    nMethods = 해당 클래스에 대해 라이브러리가 선언한 총 메서드 수.
    전체 메서드 목록 확인: get_natives(run_id).

  invoke_results (list):
    invoke plan 단계의 결과 (invoke plan 미사용 시 빈 배열). 각 항목:
      {
        "class_name": "com/example/sdk/NativeCore",
        "method_name": "dispatchEvent",
        "sig": "(IILjava/lang/String;)Ljava/lang/String;",
        "status": "called",
        "sequence_step": 1
      }
    status 값: "called" = 실행됨, "skipped" = 도달 못함, "error" = 충돌

  classes (list of str):
    이 run에서 관찰된 모든 Java 클래스 경로 (FindClass, RegisterNatives 등)
    예시: ["android/graphics/Bitmap", "com/example/sdk/NativeCore"]

다음 단계:
  get_natives(run_id)                        → 전체 등록된 메서드 목록
  get_calls(run_id)                          → 원시 JNI 호출 시퀀스
  get_calls(run_id, function="InvokeNative") → invoke 결과만
```

---

### `get_calls`

**현재:** `"Get calls from a run, optionally filtered."`

**설명:**
```
run 로그에서 JNI 호출 항목을 반환합니다 (선택적 필터 적용).
필터 없이 호출하면 모든 호출을 반환합니다 (수백 개일 수 있음).
전체 호출 타임라인을 구성하는 경우가 아니라면 항상 필터를 사용하여 결과를 좁히세요.

파라미터:
  run_id (str): list_runs에서 가져온 run 식별자.

  function (str, 선택): JNI 함수명으로 필터링. 주요 값:
    "RegisterNatives"    → 네이티브 메서드 등록 항목만
    "InvokeNative"       → 직접 invoke 결과만 (invoke plan 단계)
    "GetStaticMethodID"  → Java static 메서드 조회 (콜백 의존성)
    "FindClass"          → Java 클래스 조회

  class_name (str, 선택): Java 클래스 경로로 필터링.
    예시: "com/example/sdk/NativeCore"
    arguments 내의 class_name 필드와 매칭됩니다.

  method_name (str, 선택): 메서드명으로 필터링.
    예시: "dispatchEvent"
    arguments 내의 method_name 또는 name 필드와 매칭됩니다.

필터는 AND 조건으로 결합됩니다.
예시: function="InvokeNative" + class_name="com/example/sdk/NativeCore"
     → NativeCore invoke 결과만 반환.

반환값: call 객체 목록. 각 객체 구성:

  index (int): 순차적 호출 인덱스 (0-based). 호출 순서를 나타냅니다.

  function (str): 인터셉트된 JNI 함수명.

  caller_module (str): 이 JNI 호출을 한 .so 파일 (예: "libnative.so").

  caller_offset (str): caller_module 내 호출이 발생한 16진수 오프셋.
    예시: "0x2a1b4c"
    IDA Pro 교차 참조로 호출 코드를 찾는 데 유용합니다.

  arguments (object): 함수별 인수. 함수에 따른 주요 구조:

    핸들 값 주의: arguments에 나타나는 jclass, jmethodID, jstring, jobject 값
    (예: "jclass": "0x11000", "methodID": "0x20010")은 하네스 내부 가짜 핸들이며,
    실제 Android 런타임 주소가 아닙니다. 이 값들은 가짜 JNI 환경이 호출 체인
    (FindClass → GetMethodID → Call*Method)의 연속성을 유지하기 위해 순차적으로
    할당한 카운터입니다. 이 숫자값을 분석하거나 결론을 도출하지 마세요.
    의미 있는 필드는 항상 해석된 문자열: class_name, method_name, signature, return_string입니다.

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
      주의: 출력이 잘린 경우 methods 배열이 nMethods보다 짧을 수 있습니다.
      잘린 경우 일부 등록된 메서드가 이 목록에서 누락됩니다.

    InvokeNative:
      {
        "class_name": "com/example/sdk/NativeCore",
        "method_name": "dispatchEvent",
        "sig": "(IILjava/lang/String;)Ljava/lang/String;",
        "fnPtr": "0x7f3a2c10e8",
        "status": "called",
        "return_type": "jstring",
        "return_value": "0x80020",       ← 원시 jstring 포인터 (런타임 주소, 읽을 수 없음)
        "return_string": "ok",           ← 실제 UTF-8 문자열 내용 (이 필드를 사용하세요)
        "invoke_args": "[int:1, int:0, string:test]",
        "sequence_step": 1,              ← invoke plan에서의 위치 (0-based)
        "reason": ""
      }
      중요: return_value는 원시 메모리 포인터입니다 — 실행마다 변경되며 사람이 읽을 수 없습니다.
      메서드가 반환한 실제 문자열을 보려면 항상 return_string을 읽으세요.

      MOCK 경고: 이 run이 mock_config와 함께 실행된 경우 (run manifest에서 확인),
      return_string은 인위적으로 주입된 Java 콜백 값에 대한 라이브러리의 응답을 반영합니다 —
      실제 프로덕션 동작이 아닙니다. mock run의 반환값으로 동작 결론을 도출하지 마세요.
      mock run은 제어 흐름 차이 관찰 (어떤 JNI 호출이 발생하는지) 용도로만 사용하고,
      메서드가 "실제로" 무엇을 반환하는지 해석하는 데는 사용하지 마세요.

    GetStaticMethodID:
      {
        "class_name": "com/example/util/DeviceHelper",
        "method_name": "getDeviceToken",
        "sig": "()Ljava/lang/String;"
      }

사용 패턴 예시:
  # 실제 반환값과 함께 모든 invoke 결과 보기
  get_calls(run_id, function="InvokeNative")

  # 특정 클래스와의 모든 상호작용 보기
  get_calls(run_id, class_name="com/example/sdk/NativeCore")

  # dispatchEvent가 반환한 값 찾기
  get_calls(run_id, function="InvokeNative", method_name="dispatchEvent")

  # 라이브러리가 의존하는 모든 Java 콜백 찾기
  get_calls(run_id, function="GetStaticMethodID")
```

---

### `get_natives`

**현재:** `"Get RegisterNatives entries for a run."`

**설명:**
```
run에서 모든 RegisterNatives 항목을 반환합니다 — 라이브러리가 JVM에 등록한
네이티브 메서드의 완전한 목록. 라이브러리가 노출하는 네이티브 API를 발견하는
기본 도구입니다.

파라미터:
  run_id (str): list_runs에서 가져온 run 식별자.

반환값: 등록 객체 목록, RegisterNatives 호출당 하나. 각 객체:

  class_name (str): 이 네이티브 메서드들을 소유하는 Java 클래스 경로.
    예시: "com/example/sdk/NativeCore"

  caller_offset (str): .so에서 RegisterNatives가 호출된 16진수 오프셋.
    IDA Pro에서 등록 코드를 찾는 데 사용하세요.
    예시: "0x2a1b4c"

  nMethods (int): RegisterNatives 호출에서 선언된 총 메서드 수.
    len(methods) < nMethods인 경우, 출력이 잘렸으며 일부 메서드가 누락됩니다.
    이 경우 get_calls(run_id, function="RegisterNatives")로 원시 데이터에 접근하세요.

  methods (list): 등록된 네이티브 메서드. 각 항목:
    {
      "name": "dispatchEvent",
      "signature": "(IILjava/lang/String;)Ljava/lang/String;",
      "fnPtr": "0x7f3a2c10e8"
    }
    - name: 이 네이티브 구현에 매핑되는 Java 메서드명
    - signature: JNI 타입 디스크립터 — invoke_plan 인수를 구성하는 데 사용
    - fnPtr: 런타임 함수 포인터. 실행마다 변경됩니다. 식별자로 사용하지 마세요.

잘림(TRUNCATION) 경고:
  RegisterNatives 호출이 많은 메서드를 등록한 경우, 로그가 일부만 캡처할 수 있습니다.
  항상 len(methods)를 nMethods와 비교하세요.
  len(methods) < nMethods인 경우:
    → 일부 메서드가 숨겨져 있습니다. get_calls(run_id, function="RegisterNatives")를
      사용하면 원시 로그 항목에 추가 메서드 데이터가 있을 수 있습니다.

natives 검토 후 다음 단계:
  발견된 메서드 이름과 시그니처를 사용하여 invoke_plan을 구성하고,
  run_invoke_plan()을 호출하여 런타임 동작을 탐색하세요.
```

---

### `get_classes`

**현재:** `"Get class names observed in a run."`

**설명:**
```
이 run 중 JNI 호출에서 나타난 모든 Java 클래스 경로를 반환합니다.
라이브러리가 상호작용하는 Java 측 클래스를 빠르게 파악하는 데 유용합니다.

파라미터:
  run_id (str): list_runs에서 가져온 run 식별자.

반환값: 정렬된 Java 클래스 경로 문자열 목록.
  예시: [
    "android/graphics/Bitmap",
    "com/example/sdk/NativeCore",
    "com/example/sdk/ScriptBridge",
    "java/lang/ClassLoader",
    "java/lang/Thread"
  ]

클래스는 FindClass 호출, RegisterNatives class_name 필드,
GetStaticMethodID/GetMethodID 클래스 참조에서 추출됩니다.

참고: 이는 고수준 개요입니다. 클래스별 전체 상호작용 상세 정보는 다음을 사용하세요:
  get_calls(run_id, class_name="com/example/sdk/NativeCore")
```

---

### `diff_runs`

**현재:** `"Diff two runs by function counts and registered natives."`

**설명:**
```
두 run의 JNI 호출 횟수와 등록된 네이티브 메서드 차이를 반환합니다.
주 용도: invoke plan의 효과를 측정하기 위해 기본 run (invoke 없음)과
프로브 run (invoke 포함)을 비교합니다.

파라미터:
  run_a (str): 기본/참조 run ID (예: invoke 없는 baseline run).
  run_b (str): 비교 run ID (예: invoke plan이 있는 probe run).
  관례: run_a = 이전/baseline, run_b = 이후/probe. Delta = b - a.

반환값:

  total_calls (object):
    {"a": 66, "b": 77, "delta": 11}
    delta = run_b가 추가한 JNI 호출 수.

  function_counts (list):
    run 간에 횟수가 다른 함수만. 각 항목:
    {"function": "InvokeNative", "a": 0, "b": 4, "delta": 4}
    delta = 0인 함수는 이 목록에서 제외됩니다.
    주요 delta:
      InvokeNative delta      → run_b에서 실행된 invoke 단계 수
      GetStringUTFChars delta → invoke로 촉발된 문자열 읽기
      NewStringUTF delta      → invoke로 생성된 새 문자열 객체

  registered_natives_added (list):
    run_b의 RegisterNatives에는 있지만 run_a에는 없는 메서드.
    형식: [{"class_name": "...", "name": "...", "signature": "..."}]
    동일한 .so 파일을 분석하는 경우 보통 비어 있습니다.

  registered_natives_removed (list):
    run_a의 RegisterNatives에는 있지만 run_b에는 없는 메서드.
    동일한 .so 파일을 분석하는 경우 보통 비어 있습니다.

일반적인 워크플로우:
  run_id_base  = baseline run (JNI_OnLoad만, invoke plan 없음)
  run_id_probe = invoke run (동일 .so + invoke plan 적용)
  diff_runs(run_id_base, run_id_probe)
  → function_counts delta가 invoke plan이 추가한 것을 정확히 보여줍니다

사전 조건: diff_runs는 서로 다른 조건의 두 run이 있을 때만 의미 있습니다
(baseline vs invoke, 또는 mock 없음 vs mock).
run이 하나뿐인 경우, 먼저 run_invoke_plan으로 probe run을 생성한 후
diff_runs로 delta를 측정하세요.
```

---

### `validate_mock_config`

**현재:** `"Validate an inline mock config without running the harness."`

**설명:**
```
run_harness 또는 run_invoke_plan에서 사용하기 전에 mock config 객체를 검증합니다.
형식 오류를 조기에 발견하기 위해 mock_config를 받는 모든 실행 도구 전에 호출하세요.

파라미터:
  mock_config (object): 검증할 mock 설정 (아래 형식 참조).

Mock config 형식:
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

  규칙:
  - 각 섹션 (bool_returns, int_returns, string_returns)은 선택사항입니다.
  - 항목당 4개 필드가 모두 필요합니다: class, method, sig, return.
  - return 값 타입은 섹션 타입과 일치해야 합니다:
      bool_returns   → return은 true 또는 false여야 합니다
      int_returns    → return은 정수여야 합니다
      string_returns → return은 문자열이어야 합니다
  - class: 전체 Java 클래스 경로. 내부 클래스는 $로 구분 (예: "android/os/Build$VERSION").
  - sig: 실제 Java 메서드와 정확히 일치하는 JNI 메서드 시그니처.

반환값:
  {"status": "ok",      "errors": []}           → 유효함, 실행 도구에서 안전하게 사용 가능
  {"status": "invalid", "errors": ["message..."]} → 진행 전에 이 오류를 수정하세요

런타임에서 mock config 동작 방식:
  harness는 mock config에 나열된 메서드에 대한 GetStaticMethodID / GetMethodID 호출을
  인터셉트합니다. 라이브러리가 이후 해당 메서드에 Call*Method를 호출하면,
  harness는 실제 Java 반환값 대신 설정된 가짜 값을 반환합니다.

  중요 제한 사항: Mock은 JNI 계층 Java 콜백만 영향을 미칩니다 (Call*Method 인터셉트 패턴).
  JNI 브릿지를 거치지 않고 순수하게 Dalvik/ART 내에서 호출되는 메서드에는 영향을 주지 않습니다.
  mock된 메서드에서 diff_runs delta=0이 나온다면, 라이브러리가 JNI가 아닌 Java 측에서
  해당 메서드를 호출할 가능성이 높습니다.
```

---

## 실행 도구

> 이 도구들은 MCP 서버가 `--allow-execute`로 시작되어야 합니다.
> adb를 통해 연결된 Android 기기에 파일을 푸시하고 명령을 실행합니다.

---

### `run_harness`

**현재:** `"Run the configured Android harness for JNI_OnLoad observation."`

**설명:**
```
연결된 Android 기기의 대상 .so 파일에 JNI harness를 실행합니다.
JNI_OnLoad (라이브러리 초기화) 중 발생한 모든 JNI 호출을 기록합니다.
이는 기본 관찰 run입니다 — 시작 중 라이브러리가 내부적으로 호출하는 것 외에는
메서드가 직접 invoke되지 않습니다.

파라미터:
  so_name (str): 설정된 libs_dir 내 대상 .so 파일명.
    예시: "libnative.so"  (전체 경로가 아닌 파일명만)

  label (str, 선택): 이 run에 대한 사람이 읽을 수 있는 태그.
    run_id에 포함되어 식별에 용이합니다. 예시: "baseline_v2"

  mock_config (object, 선택): 시작 중 Java 콜백 반환값 조작.
    여기에 전달하기 전에 validate_mock_config()로 검증하세요.

  timeout_sec (int, 선택): 실행 타임아웃 (초).
    서버 설정 타임아웃 기본값 (보통 30초).
    라이브러리 초기화가 느린 경우 증가시키세요.

반환값:
  {
    "status": "ok" | "failed",
    "run_id": "20240101_115800_baseline",   ← get_natives, get_calls, diff_runs에 저장하세요
    "run_dir": "/local/path/to/run/dir",
    "returncode": 0,                        ← 0이 아니면 harness 오류
    "duration_sec": 3.1,
    "summary": { ...get_summary 출력과 동일한 필드... },
    "manifest": { ...전체 run 메타데이터... },
    "next_tools": ["get_run", "get_summary", "get_calls", "diff_runs"]
  }

이 호출 후:
  get_natives(run_id)  → 라이브러리가 등록한 메서드 확인
  get_calls(run_id)    → 시작 중 전체 JNI 호출 시퀀스 확인

일반적인 사용: 먼저 이를 실행하여 baseline을 설정하고,
동일한 .so로 run_invoke_plan을 실행하여 diff_runs로 동작을 비교하세요.
```

---

### `run_invoke_plan`

**현재:** `"Run the configured Android harness with an inline invoke plan."`

**설명:**
```
JNI_OnLoad 이후 일련의 직접 네이티브 메서드 호출과 함께 JNI harness를 실행합니다.
등록된 네이티브 메서드를 능동적으로 탐색하고 런타임 동작, 반환값, 부작용을
JNI_OnLoad만으로는 알 수 없는 부분까지 관찰하는 데 사용하세요.

사전 조건: 유효한 메서드 이름과 시그니처를 발견하려면 먼저 get_natives()를 호출하세요.

언제 사용해야 하는가:
  get_natives에서 의심스러운 메서드 발견 시 — 입력 주입 (tap, sendKeyEvent,
  onTouchEvent), 화면 캡처 (obtainImage*, snapShotJava), 이벤트 디스패치
  (sendEvent, pluginEvent), 또는 런타임 동작이 불분명한 모든 메서드 —
  수동 관찰에서 멈추지 마세요. 해당 메서드를 직접 실행하여 동작을 확인하세요.
  RegisterNatives 수동 관찰만으로는 런타임 동작을 확인할 수 없습니다.

파라미터:
  so_name (str): libs_dir 내 대상 .so 파일명.
    예시: "libnative.so"

  invoke_plan (list): 메서드 호출 단계의 순서 목록. 각 단계:
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

    필드 상세:
      class:     Java 클래스 경로 — get_natives가 반환한 것과 정확히 일치해야 함
      method:    메서드명 — get_natives가 반환한 것과 정확히 일치해야 함
      signature: JNI 시그니처 — get_natives가 반환한 것과 정확히 일치해야 함
      args:      시그니처 파라미터 타입과 일치하는 순서대로 나열된 인수 목록

    지원되는 인수 타입과 JNI 동등값:
      "int"    → I (Java int)
      "long"   → J (Java long)
      "bool"   → Z (Java boolean), 값은 true 또는 false여야 함
      "float"  → F (Java float)
      "double" → D (Java double)
      "string" → Ljava/lang/String; (Java String)

    실행 규칙:
      - 단계는 배열 순서로 실행됩니다.
      - 단계가 충돌 또는 시그널을 일으키면 나머지 단계는 모두 건너뜁니다.
      - 초기화된 컨텍스트 객체가 필요한 메서드 (long/J 파라미터로 전달)는
        동일한 plan의 이전 단계에서 해당 컨텍스트가 설정되어야 합니다.
        null 포인터(0)를 전달하면 충돌이 발생합니다 — 해당 메서드를 제외하거나
        초기화가 먼저 이루어지도록 하세요.

  label (str, 선택): 이 run의 태그. 예시: "probe_dispatch"

  mock_config (object, 선택): invoke 실행 중 Java 콜백 인터셉트.
    먼저 validate_mock_config()로 검증하세요.
    경고: mock_config를 사용하면 Java 콜백 반환값이 인위적입니다.
    이 run에서 기록된 네이티브 메서드 반환값은 가짜 입력에 대한 응답이므로
    실제 동작 데이터로 사용해서는 안 됩니다. 결과 run은 제어 흐름 diff 분석
    (diff_runs)에만 사용하고, 반환값 분석에는 사용하지 마세요.

  timeout_sec (int, 선택): 기본 실행 타임아웃 재정의.

반환값:
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

invoke 단계의 실제 반환값 읽기:
  get_calls(run_id, function="InvokeNative")
  → return_string 필드를 읽으세요 (원시 포인터인 return_value가 아님)

이 invoke plan이 baseline run에 추가한 것 측정:
  diff_runs(baseline_run_id, this_run_id)

Call*Method 모니터링 (가장 깊은 관찰 레이어):
  run_invoke_plan 완료 후 반드시 Java 역호출 시도를 확인하세요:
  1. get_summary(run_id) → top_functions에서 "Call"로 시작하는 항목 확인
     예시: {"function": "CallStaticObjectMethodV", "count": 1}
  2. 발견된 각 Call* 항목에 대해:
     get_calls(run_id, function="<top_functions의 exact name>")
     → 네이티브가 Java로 역호출 시도한 class_name과 method_name 확인
  이것이 관찰 경계입니다 — 어떤 Java 메서드가 요청되었는지는 볼 수 있지만,
  그 메서드가 실제로 하는 일은 볼 수 없습니다 (하네스에서 실제 Java 미실행).
  이 경계 너머를 추적하려면 DEX 트레이스 또는 IDA Pro가 필요합니다.
```

---

### `rerun_with_mock`

**현재:** `"Rerun a base run with an inline mock config and return the diff."`

**설명:**
```
mock config를 적용하여 기존 run을 재실행하고, 새 run 결과와 원본과의 diff를 반환합니다.
Java 콜백 반환값이 변경될 때 네이티브 라이브러리가 어떻게 동작하는지 테스트하는 데 사용합니다
— 예: 검사 우회 또는 기기 속성 대체.

이 도구는 내부적으로 run_harness (또는 run_invoke_plan) + diff_runs를 하나로 결합합니다.

파라미터:
  base_run_id (str): 재실행할 원본 run. run store에 존재해야 합니다.
    동일한 .so 파일이 재사용됩니다. base run에 invoke plan이 있었다면
    기본적으로 재사용됩니다 (reuse_invoke_plan으로 제어).

  mock_config (object): Java 콜백 인터셉트 설정.
    먼저 validate_mock_config()로 검증하세요. 형식 상세는 해당 도구 참조.

    예시 — Java 콜백이 반환하는 기기 모델 문자열 대체:
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

  label (str, 선택): 새 실험 run의 태그.

  reuse_invoke_plan (bool, 기본값=true):
    true  → base run에 invoke plan이 있었다면 재사용 (권장)
    false → JNI_OnLoad 초기화만 재실행, invoke 단계 건너뜀

  timeout_sec (int, 선택): 타임아웃 재정의. 기본값은 base run의 원래 타임아웃.

반환값:
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

diff 결과 해석:
  모든 함수에서 delta = 0:
    mock이 JNI 호출 횟수에 관찰 가능한 영향을 미치지 않았습니다. 이는 라이브러리가
    JNI Call*Method 브릿지가 아닌 Java/Dalvik 측에서 mock된 Java 메서드를 호출할 때
    예상되는 동작입니다. Mock은 JNI 계층 호출만 인터셉트합니다.

  특정 함수에서 delta > 0:
    mock이 라이브러리의 제어 흐름을 변경했습니다 — Java 콜백 결과가 이후 발생한
    JNI 호출에 영향을 미쳤습니다 (예: 검사 우회).

  횟수 이상의 동작 차이 검사:
    get_calls(experiment_run_id, function="InvokeNative")
    로 base run과 return_string 값을 비교하세요.

주의 — MOCK RUN 반환값을 잘못 해석하지 마세요:
  실험 run은 인위적으로 주입된 Java 콜백 값과 함께 실행되었습니다.
  실험 run의 InvokeNative return_string 값은 가짜 입력에 대한 응답이며,
  라이브러리의 실제 프로덕션 동작이 아닙니다.
  - mock run은 JNI 호출 패턴의 변화 관찰 (diff 결과)에만 사용하세요.
  - 실험 run의 반환값을 의미 있는 분석 결과로 취급하지 마세요.
  - 실제 반환값 분석이 필요하다면 mock 없는 run (mock_config 없이
    run_harness 또는 run_invoke_plan)을 사용하세요.
```

---

## 요약: 현재 vs 적용된 품질

| 도구 | 현재 설명 | 문제점 | 우선순위 |
|------|-----------|--------|----------|
| `list_runs` | "List available jni-tracer runs." | 출력 필드 없음, 다음 단계 안내 없음 | 높음 |
| `get_run` | "Get a run manifest." | 필드 목록 없음, get_summary와 사용 시점 불분명 | 낮음 |
| `get_summary` | "Get a run summary." | 필드 설명 전혀 없음 | **심각** |
| `get_calls` | "Get calls from a run, optionally filtered." | 필터 사용법 미설명, 필드 가이드 없음 | **심각** |
| `get_natives` | "Get RegisterNatives entries for a run." | 잘림 경고 없음, fnPtr 경고 없음 | **심각** |
| `get_classes` | "Get class names observed in a run." | 최소이나 허용 가능 | 낮음 |
| `diff_runs` | "Diff two runs by function counts..." | run_a/run_b 순서 관례 미설명 | 높음 |
| `validate_mock_config` | "Validate an inline mock config..." | mock 형식 미표시, mock 메커니즘 미설명 | 높음 |
| `run_harness` | "Run the configured Android harness..." | 출력 필드 가이드 없음, 워크플로우 컨텍스트 없음 | 높음 |
| `run_invoke_plan` | "Run the configured Android harness with an inline invoke plan." | invoke_plan 형식 완전 누락 | **심각** |
| `rerun_with_mock` | "Rerun a base run with an inline mock config..." | mock 형식 누락, diff 해석 누락 | 높음 |
| FastMCP instructions | 한 문장 | 워크플로우 없음, 시그니처 가이드 없음, 주의사항 없음 | **심각** |

---

## 소스 검토에서 발견한 핵심 인사이트

1. **InvokeNative의 `return_string` vs `return_value`**
   `get_calls(function="InvokeNative")`은 두 필드를 모두 반환합니다:
   - `return_value`: 원시 jstring 포인터 (예: `"0x80020"`) — 실행마다 변경, 읽을 수 없음
   - `return_string`: 메서드가 반환한 실제 UTF-8 문자열 (예: `"ok"`) — 이를 사용하세요
   현재 어떤 설명에도 이 구분이 언급되지 않습니다. `return_value`만 읽는 LLM은
   메모리 주소를 보고 메서드가 포인터를 반환했다고 잘못 결론 내릴 것입니다.

2. **`get_natives`의 조용한 잘림(Truncation)**
   RegisterNatives 호출이 많은 메서드를 등록한 경우, 로그가 일부만 캡처할 수 있습니다.
   `nMethods`는 선언된 총수를 반영하고; `methods` 배열 길이는 더 작을 수 있습니다.
   현재 어떤 설명에도 이에 대한 경고가 없습니다. LLM은 메서드 목록이 완전하다고 가정하고
   invoke plan 구성 시 등록된 메서드를 놓칠 수 있습니다.

3. **실행 도구 응답의 `next_tools` 필드**
   `run_harness`, `run_invoke_plan`, `rerun_with_mock`은 이미 JSON 응답에
   `next_tools` 목록을 포함하고 있습니다 (예: `["get_run", "get_summary", "get_calls", "diff_runs"]`).
   이는 LLM에 유용한 안내입니다 — 하지만 어떤 설명에도 이 필드가 존재한다는 언급이 없습니다.

4. **Mock 타이밍 제한**
   Mock은 JNI 계층 Java 메서드 호출만 인터셉트합니다 (GetStaticMethodID → Call*Method 패턴).
   순수하게 Dalvik/ART 내에서 호출되는 Java 메서드에는 영향이 없습니다.
   현재 설명들은 이를 설명하지 않아, mock이 delta=0을 생성할 때 혼란을 유발합니다.

5. **`reuse_invoke_plan` 기본값 `true`**
   `rerun_with_mock`에서 `reuse_invoke_plan=false`를 명시적으로 전달하지 않는 한
   base run의 invoke plan이 자동으로 재사용됩니다. 이는 문서화되지 않았습니다 —
   LLM은 실험 run이 base run에서 상속된 invoke 단계를 포함한다는 사실을 모를 수 있습니다.
