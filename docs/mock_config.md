# Mock Config — 런타임 리턴값 주입

`--mock` 옵션으로 JSON 설정 파일을 제공하면, 네이티브 SO가 호출하는 Java 메서드(Call*Method)의 **리턴값을 재컴파일 없이 원하는 값으로 교체**할 수 있습니다.

---

## 왜 필요한가

현재 하네스는 `CallBooleanMethod`, `CallIntMethod` 등의 스텁이 항상 `0`/`false`/`NULL`을 반환합니다.
타겟 SO가 리턴값으로 분기하는 코드가 있으면 분석자는 **항상 같은 경로만** 탐색하게 됩니다.

```
// 예시: 루팅 탐지 로직
jboolean rooted = env->CallBooleanMethod(checker, isRootedId);
if (rooted) {
    // 악성 행위 A  ← mock 없이는 영원히 진입 불가
} else {
    // 악성 행위 B  ← 항상 여기만 실행됨
}
```

Mock 설정을 사용하면 `isRooted → true`로 주입하여 **분기 A까지 추적**할 수 있습니다.

---

## 기본 사용법

```bash
# 기본 실행 (mock 없음)
./jni_harness target/libnative.so

# mock 설정 주입
./jni_harness target/libnative.so --mock mock.json

# 단축 옵션
./jni_harness target/libnative.so -m mock.json
```

Android 디바이스에서:
```bash
adb shell "cd /data/local/tmp && ./jni_harness_arm64_android ./libnative.so --mock mock.json"
```

---

## 설정 파일 형식

```json
{
  "bool_returns": [
    {
      "class":  "com/security/RootChecker",
      "method": "isRooted",
      "sig":    "()Z",
      "return": true
    }
  ],
  "int_returns": [
    {
      "class":  "android/os/Build$VERSION",
      "method": "getSdkInt",
      "sig":    "()I",
      "return": 34
    }
  ],
  "string_returns": [
    {
      "class":  "android/os/Build",
      "method": "getModel",
      "sig":    "()Ljava/lang/String;",
      "return": "Pixel 8"
    }
  ]
}
```

### 필드 설명

| 필드 | 설명 |
|------|------|
| `class` | JNI 형식의 클래스명 (`.` 대신 `/` 사용) |
| `method` | 메서드명 |
| `sig` | JNI 메서드 시그니처 |
| `return` | 주입할 리턴값 |

### 지원 타입

| 섹션 | 대응 JNI 호출 | 리턴값 예시 |
|------|--------------|------------|
| `bool_returns` | `CallBooleanMethod`, `CallStaticBooleanMethod` | `true`, `false` |
| `int_returns` | `CallIntMethod`, `CallByteMethod`, `CallShortMethod`, `CallLongMethod`, `CallStaticIntMethod` 등 | `34`, `-1`, `0` |
| `string_returns` | `CallObjectMethod`, `CallStaticObjectMethod` (String 반환) | `"Pixel 8"` |

---

## 실전 시나리오

### 1. 루팅 / 에뮬레이터 탐지 우회

```json
{
  "bool_returns": [
    { "class": "com/scottyab/rootbeer/RootBeer", "method": "isRooted",    "sig": "()Z", "return": false },
    { "class": "com/example/util/DeviceCheck",   "method": "isEmulator",  "sig": "()Z", "return": false },
    { "class": "com/example/util/DeviceCheck",   "method": "isDebuggable","sig": "()Z", "return": false }
  ]
}
```

### 2. Android 기기 환경 시뮬레이션

```json
{
  "string_returns": [
    { "class": "android/os/Build", "method": "getModel",        "sig": "()Ljava/lang/String;", "return": "Pixel 8 Pro" },
    { "class": "android/os/Build", "method": "getManufacturer", "sig": "()Ljava/lang/String;", "return": "Google" },
    { "class": "android/os/Build", "method": "getBrand",        "sig": "()Ljava/lang/String;", "return": "google" }
  ],
  "int_returns": [
    { "class": "android/os/Build$VERSION", "method": "getSdkInt", "sig": "()I", "return": 34 }
  ]
}
```

### 3. 라이선스 / 인증 우회

```json
{
  "bool_returns": [
    { "class": "com/app/license/LicenseManager", "method": "isLicensed",  "sig": "()Z", "return": true  },
    { "class": "com/app/auth/AuthManager",        "method": "isLoggedIn",  "sig": "()Z", "return": true  }
  ],
  "int_returns": [
    { "class": "com/app/auth/AuthManager", "method": "getAuthLevel", "sig": "()I", "return": 99 }
  ]
}
```

### 4. 분기 A/B 전환 비교 분석

같은 SO를 `mock_root_true.json` / `mock_root_false.json`으로 각각 실행하여 두 JSON 로그를 비교:

```bash
./jni_harness -m mock_root_true.json  libnative.so
cp logs/jni_hook.json logs/trace_rooted.json

./jni_harness -m mock_root_false.json libnative.so
cp logs/jni_hook.json logs/trace_clean.json

diff logs/trace_rooted.json logs/trace_clean.json
```

---

## 로그에서 Mock 확인

mock이 적용되면 텍스트 로그에 `[MOCK]` 태그가 출력됩니다:

```
[JNI #0012] [1234.567890] CallBooleanMethod(obj=0x5001, com/security/RootChecker.isRooted()Z)
[INFO]   [MOCK] CallBooleanMethod com/security/RootChecker.isRooted()Z => 1
[JNI #0013] [1234.567910] CallObjectMethod(obj=0x5001, android/os/Build.getModel()Ljava/lang/String;)
[INFO]   [MOCK] CallObjectMethod android/os/Build.getModel()Ljava/lang/String; => "Pixel 8 Pro"
```

---

## JNI 시그니처 빠른 참고

| Java 타입 | JNI 시그니처 |
|-----------|-------------|
| `boolean` | `Z` |
| `int`     | `I` |
| `long`    | `J` |
| `String`  | `Ljava/lang/String;` |
| `void`    | `V` |
| `boolean[]` | `[Z` |

메서드 시그니처 형식: `(인자타입들)리턴타입`

```
()Z                          → boolean 반환, 인자 없음
(I)Z                         → int 인자, boolean 반환
()Ljava/lang/String;         → String 반환, 인자 없음
(Ljava/lang/String;I)V       → String + int 인자, void 반환
```

> **팁:** `javap -s -p ClassName` 명령으로 클래스의 전체 시그니처를 확인할 수 있습니다.

---

## 주의사항

- **클래스명은 `/` 구분자 사용** — `com.example.MyClass` ❌ → `com/example/MyClass` ✓
- **시그니처 오타 주의** — 매칭은 정확한 문자열 비교
- **`string_returns`는 String을 반환하는 메서드에만 적용** — int 반환 메서드에 string mock을 설정해도 무시됨
- **최대 256개 엔트리** 지원
- mock이 로드되지 않아도 하네스는 정상 실행됨 (mock 없는 기본 동작으로 폴백)
