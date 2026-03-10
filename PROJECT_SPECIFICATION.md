# Android SO 파일 JNI 함수 후킹 하네스 프로그램 개발 명세서

## 📋 프로젝트 개요

### 목적
Android `.so` (Shared Object) 라이브러리 파일 내부에서 호출되는 JNI(Java Native Interface) 함수들의 인자를 동적으로 후킹하여 로깅하는 분석 도구 개발.

### 핵심 목표
- **주요 타겟**: `FindClass`, `GetMethodID`를 포함한 모든 JNI 함수 호출 인자 캡처
- **환경**: 실제 Android VM 없이 ELF 하네스 환경에서 독립 실행
- **출력**: JNI 함수 호출 시 인자 및 컨텍스트 정보를 콘솔에 로깅

### 배경
- Android 앱의 네이티브 라이브러리(`.so`)는 JNI를 통해 Java 코드와 상호작용
- 정적 분석으로는 파악하기 어려운 동적 호출 패턴 분석 필요
- APK 빌드 없이 `.so` 파일만으로 분석 가능한 경량 도구 필요

### ⚙️ 실행 환경

#### 필수 요구사항

**이 하네스 프로그램은 Linux 기반 시스템에서만 실행됩니다.**

이유:
- `dlopen()`, `dlsym()` 등 Linux 시스템 콜 사용
- ELF (Executable and Linkable Format) 바이너리 로더 필요
- POSIX 호환 환경 필요

#### 지원 플랫폼

✅ **Linux (네이티브)**
- Ubuntu, Debian, Fedora, Arch Linux 등
- ARM64 Linux 서버 또는 Raspberry Pi
- x86_64 일반 PC/노트북
- **권장**: 개발 및 분석에 가장 편리

✅ **Android OS (adb shell)**
- 실제 Android 기기 (ARM64)
- Android 에뮬레이터 (x86_64)
- **장점**: 네이티브 환경, SO의 원래 실행 환경
- **단점**: adb 연결 필요, 파일 전송 필요, root 권한 필요할 수 있음

✅ **Windows (WSL2 사용)**
- Windows 10/11 + WSL2 (Windows Subsystem for Linux)
- WSL2 내부는 실제 Linux 커널 사용
- **주의**: WSL1은 불가능 (시스템 콜 에뮬레이션 불완전)

❌ **Windows (네이티브)**
- `dlopen`이 없음 (LoadLibrary는 다른 API)
- ELF 바이너리를 직접 로드할 수 없음
- **해결책**: WSL2 사용

#### 권장 실행 환경

| 사용 목적 | 권장 환경 | 이유 |
|----------|----------|------|
| 개발 및 테스트 | Linux PC/VM | 편리함, 디버깅 용이 |
| 대량 분석 | Linux 서버 | 성능, 자동화 |
| 실제 기기 검증 | Android adb shell | 원래 실행 환경 |
| Windows 사용자 | WSL2 | Windows에서 Linux 환경 사용 |

### ⚙️ 아키텍처 지원

이 하네스 프로그램은 **ARM64 (aarch64)** 와 **x86_64** 아키텍처를 모두 지원합니다.

#### 중요 제약사항

**하네스 프로그램의 아키텍처와 타겟 SO 파일의 아키텍처가 반드시 일치해야 합니다.**

```
하네스(ARM64) + 타겟 SO(ARM64) ✅ 가능
하네스(x86_64) + 타겟 SO(x86_64) ✅ 가능
하네스(ARM64) + 타겟 SO(x86_64) ❌ 불가능 (dlopen 실패)
하네스(x86_64) + 타겟 SO(ARM64) ❌ 불가능 (dlopen 실패)
```

#### 아키텍처별 사용 시나리오

1. **ARM64 (aarch64)** - 주요 사용 사례
   - 실제 Android 기기 (대부분의 현대 스마트폰)

2. **x86_64 (amd64)** - 개발/테스트 환경
   - Android 에뮬레이터 (AVD x86_64 이미지)
   - 일반 Linux/Windows 개발 머신
   - Chromebook (Intel CPU)

---

## 🔧 기술적 배경 지식

### JNI 동작 원리

#### 1. JNIEnv 구조
```c
struct JNIEnv_ {
    const struct JNINativeInterface* functions;
};
```

- `JNIEnv`는 함수 포인터 테이블(`JNINativeInterface`)을 가리키는 포인터
- 모든 JNI 함수는 이 테이블을 통해 간접 호출됨
- 예: `env->FindClass(env, "com/example/MyClass")`는 실제로 `env->functions->FindClass(env, "com/example/MyClass")`로 호출

#### 2. JNI_OnLoad
```c
jint JNI_OnLoad(JavaVM* vm, void* reserved);
```

- Android 시스템이 `.so`를 로드할 때 자동 호출하는 초기화 함수
- `System.loadLibrary()`가 내부적으로 호출
- `dlopen()`만으로는 자동 실행되지 않으므로 **수동 호출 필요**

#### 3. .init_array 섹션
- ELF 파일의 초기화 함수 배열
- `dlopen()` 시점에 **자동 실행**됨
- C++ 전역 생성자 등이 여기에 등록됨

### 왜 가짜 JNIEnv가 필요한가?

실제 Android VM 환경이 없으므로:
1. `FindClass` → Dalvik/ART VM의 클래스 로더 필요 → 없으면 실패
2. `GetMethodID` → 클래스 메타데이터 접근 필요 → 없으면 실패
3. **해결책**: 모든 JNI 함수를 stub으로 구현하여 호출만 가로채고 인자를 로깅

---

## 🏗️ 시스템 아키텍처

### 전체 구조
```
┌─────────────────────────────────────┐
│   하네스 프로그램 (main.c)           │
│                                     │
│  ┌───────────────────────────────┐ │
│  │ 1. dlopen("libtarget.so")     │ │
│  └───────────────────────────────┘ │
│             ↓                       │
│  ┌───────────────────────────────┐ │
│  │ 2. .init_array 자동 실행      │ │
│  └───────────────────────────────┘ │
│             ↓                       │
│  ┌───────────────────────────────┐ │
│  │ 3. dlsym("JNI_OnLoad")        │ │
│  │    + 가짜 JavaVM 생성          │ │
│  └───────────────────────────────┘ │
│             ↓                       │
│  ┌───────────────────────────────┐ │
│  │ 4. JNI_OnLoad(fake_vm, NULL)  │ │
│  └───────────────────────────────┘ │
└──────────────┬──────────────────────┘
               │
               ↓
┌──────────────────────────────────────┐
│   Target SO (libtarget.so)           │
│                                      │
│  JNI_OnLoad() {                      │
│    JNIEnv* env = ...;                │
│    env->FindClass(...)      ───┐     │
│    env->GetMethodID(...)       │     │
│  }                             │     │
└────────────────────────────────┼─────┘
                                 │
                                 ↓
                    ┌────────────────────────┐
                    │   Fake JNIEnv          │
                    │   (fake_jni.c)         │
                    │                        │
                    │  fake_FindClass()      │
                    │  fake_GetMethodID()    │
                    │  fake_CallObjectMethod │
                    │  ... (200+ 함수)       │
                    └────────┬───────────────┘
                             │
                             ↓
                    ┌─────────────────┐
                    │  로그 출력       │
                    │  stdout/file    │
                    └─────────────────┘
```

### 데이터 흐름
1. **로딩 단계**: 하네스가 target SO를 메모리에 로드
2. **초기화 단계**: `.init_array` 실행 → `JNI_OnLoad` 수동 호출
3. **후킹 단계**: Target SO가 JNI 함수 호출 → Fake 함수로 리다이렉트
4. **로깅 단계**: 인자 정보 추출 및 출력

---

## 📁 프로젝트 파일 구조

```
AOS_haness_project/
├── src/
│   ├── main.c                    # 메인 하네스 로더
│   ├── fake_jni.c                # JNIEnv stub 구현
│   ├── fake_jni.h                # JNI stub 헤더
│   ├── jni_logger.c              # 로깅 유틸리티
│   ├── jni_logger.h              # 로깅 헤더
│   ├── json_logger.c             # JSON 포맷 로거
│   └── json_logger.h             # JSON 로거 헤더
├── include/
│   └── jni.h                     # Android JNI 헤더 (복사본)
├── build/                        # 빌드 출력
├── target/                       # 분석 대상 .so 파일 위치
├── logs/                         # 로그 출력 폴더
│   ├── jni_hook.log              # 텍스트 로그
│   └── jni_hook.json             # JSON 로그
├── Makefile                      # 빌드 설정
├── README.md                     # 사용자 가이드
└── PROJECT_SPECIFICATION.md      # 본 문서
```

---

## 💻 구현 상세 명세

### 1. main.c - 하네스 로더

#### 역할
- 대상 SO 파일 로드
- 가짜 JavaVM 및 JNIEnv 설정
- JNI_OnLoad 호출
- 메모리 정리

#### 구현 세부사항

```c
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include "jni.h"
#include "fake_jni.h"
#include "jni_logger.h"

// JNI_OnLoad 함수 포인터 타입
typedef jint (*JNI_OnLoad_t)(JavaVM* vm, void* reserved);

// 가짜 JavaVM 구조체 (최소 구현)
typedef struct {
    const struct JNIInvokeInterface* functions;
} FakeJavaVM;

// JavaVM 함수 테이블 (필요시 구현)
static struct JNIInvokeInterface fake_vm_funcs = {
    NULL, NULL, NULL, // reserved
    NULL, // DestroyJavaVM
    NULL, // AttachCurrentThread
    NULL, // DetachCurrentThread
    NULL, // GetEnv
    NULL  // AttachCurrentThreadAsDaemon
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <target_so_path>\n", argv[0]);
        return 1;
    }

    const char* so_path = argv[1];
    
    // 로거 초기화 (텍스트 + JSON)
    init_logger("./logs/jni_hook.log");
    init_json_logger("./logs/jni_hook.json");
    log_info("=== JNI Harness Started ===");
    
    // 아키텍처 정보 출력
#ifdef ARCH_ARM64
    log_info("Harness Architecture: ARM64 (aarch64)");
#elif defined(ARCH_X86_64)
    log_info("Harness Architecture: x86_64 (amd64)");
#else
    log_info("Harness Architecture: Unknown");
#endif
    
    log_info("Target SO: %s", so_path);

    // 1. SO 파일 로드
    void* handle = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        log_error("dlopen failed: %s", dlerror());
        return 1;
    }
    log_info("SO loaded successfully");
    log_info(".init_array functions executed automatically");

    // 2. JNI_OnLoad 심볼 찾기
    JNI_OnLoad_t jni_onload = (JNI_OnLoad_t)dlsym(handle, "JNI_OnLoad");
    if (!jni_onload) {
        log_warning("JNI_OnLoad not found in SO");
        log_info("Skipping JNI_OnLoad call");
    } else {
        log_info("JNI_OnLoad found at %p", (void*)jni_onload);

        // 3. 가짜 JavaVM 준비
        FakeJavaVM fake_vm;
        fake_vm.functions = &fake_vm_funcs;

        // 4. 가짜 JNIEnv 준비 (fake_jni.c에서 제공)
        JNIEnv* fake_env = create_fake_jnienv();
        
        log_info("Calling JNI_OnLoad with fake JavaVM...");
        
        // 5. JNI_OnLoad 호출
        jint version = jni_onload((JavaVM*)&fake_vm, NULL);
        
        log_info("JNI_OnLoad returned version: 0x%x", version);
    }

    // 6. 추가 분석 함수 호출 (옵션)
    // 예: dlsym으로 다른 exported 함수를 찾아 호출

    // 7. 로그 요약 출력
    print_log_summary();

    // 8. 정리
    log_info("=== JNI Harness Finished ===");
    close_json_logger();
    close_logger();
    dlclose(handle);

    return 0;
}
```

#### 핵심 포인트
- **RTLD_NOW**: 모든 심볼을 즉시 리졸브 (lazy loading 방지)
- **RTLD_GLOBAL**: 심볼을 전역 네임스페이스에 추가
- **에러 처리**: 각 단계마다 실패 시 상세 로그

---

### 2. fake_jni.c - JNI 함수 Stub 구현

#### 역할
- JNINativeInterface 전체 함수 테이블 구현
- 각 함수 호출 시 인자 로깅
- 안전한 더미 값 반환

#### 주요 함수 카테고리

JNI 함수는 약 200개 이상이며, 다음 카테고리로 분류:

1. **Version Information** (1개)
   - `GetVersion`

2. **Class Operations** (11개)
   - `DefineClass`, `FindClass`, `GetSuperclass`, `IsAssignableFrom` 등

3. **Exception Handling** (9개)
   - `Throw`, `ThrowNew`, `ExceptionOccurred` 등

4. **Global/Local References** (9개)
   - `NewGlobalRef`, `DeleteGlobalRef`, `NewLocalRef` 등

5. **Object Operations** (5개)
   - `AllocObject`, `NewObject`, `GetObjectClass` 등

6. **Method Access** (17개)
   - `GetMethodID`, `CallObjectMethod`, `CallVoidMethod` 등

7. **Field Access** (18개)
   - `GetFieldID`, `GetObjectField`, `SetIntField` 등

8. **Static Method/Field** (17개)
   - `GetStaticMethodID`, `CallStaticVoidMethod` 등

9. **String Operations** (6개)
   - `NewStringUTF`, `GetStringUTFChars` 등

10. **Array Operations** (36개)
    - `GetArrayLength`, `NewObjectArray`, `GetIntArrayElements` 등

11. **Register Natives** (2개)
    - `RegisterNatives`, `UnregisterNatives`

12. **Monitor Operations** (2개)
    - `MonitorEnter`, `MonitorExit`

13. **NIO Support** (2개)
    - `NewDirectByteBuffer`, `GetDirectBufferAddress`

14. **Reflection Support** (4개)
    - `FromReflectedMethod`, `ToReflectedMethod` 등

#### 구현 템플릿

```c
#include "fake_jni.h"
#include "jni_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================
// 핵심 후킹 함수들 (상세 로깅)
// ============================================

static jclass JNICALL fake_FindClass(JNIEnv* env, const char* name) {
    // 텍스트 로그
    log_jni_call("FindClass", "name=%s", name ? name : "NULL");
    
    // JSON 로그 - 상세 인자 정보
    const char* arg_names[] = {"env", "name"};
    char env_str[32], name_str[256];
    snprintf(env_str, sizeof(env_str), "%p", (void*)env);
    snprintf(name_str, sizeof(name_str), "%s", name ? name : "NULL");
    const char* arg_values[] = {env_str, name_str};
    log_jni_call_json("FindClass", arg_names, arg_values, 2);
    
    // 통계 업데이트
    increment_call_count("FindClass");
    
    // 더미 jclass 반환 (NULL이면 target SO가 에러 처리할 수 있음)
    // 분석 목적이므로 0x1000 같은 고정된 더미 포인터 반환
    return (jclass)0x1000; 
}

static jmethodID JNICALL fake_GetMethodID(
    JNIEnv* env, 
    jclass clazz, 
    const char* name, 
    const char* sig
) {
    // 텍스트 로그
    log_jni_call("GetMethodID", 
                 "clazz=%p, name=%s, sig=%s", 
                 clazz, 
                 name ? name : "NULL", 
                 sig ? sig : "NULL");
    
    // JSON 로그 - 모든 인자 캡처
    const char* arg_names[] = {"env", "clazz", "name", "sig"};
    char env_str[32], clazz_str[32], name_str[256], sig_str[256];
    snprintf(env_str, sizeof(env_str), "%p", (void*)env);
    snprintf(clazz_str, sizeof(clazz_str), "%p", (void*)clazz);
    snprintf(name_str, sizeof(name_str), "%s", name ? name : "NULL");
    snprintf(sig_str, sizeof(sig_str), "%s", sig ? sig : "NULL");
    const char* arg_values[] = {env_str, clazz_str, name_str, sig_str};
    log_jni_call_json("GetMethodID", arg_names, arg_values, 4);
    
    increment_call_count("GetMethodID");
    
    return (jmethodID)0x2000;
}

static jfieldID JNICALL fake_GetFieldID(
    JNIEnv* env,
    jclass clazz,
    const char* name,
    const char* sig
) {
    // 텍스트 로그
    log_jni_call("GetFieldID",
                 "clazz=%p, name=%s, sig=%s",
                 clazz,
                 name ? name : "NULL",
                 sig ? sig : "NULL");
    
    // JSON 로그
    const char* arg_names[] = {"env", "clazz", "name", "sig"};
    char env_str[32], clazz_str[32], name_str[256], sig_str[256];
    snprintf(env_str, sizeof(env_str), "%p", (void*)env);
    snprintf(clazz_str, sizeof(clazz_str), "%p", (void*)clazz);
    snprintf(name_str, sizeof(name_str), "%s", name ? name : "NULL");
    snprintf(sig_str, sizeof(sig_str), "%s", sig ? sig : "NULL");
    const char* arg_values[] = {env_str, clazz_str, name_str, sig_str};
    log_jni_call_json("GetFieldID", arg_names, arg_values, 4);
    
    increment_call_count("GetFieldID");
    
    return (jfieldID)0x3000;
}

static jint JNICALL fake_RegisterNatives(
    JNIEnv* env,
    jclass clazz,
    const JNINativeMethod* methods,
    jint nMethods
) {
    // 텍스트 로그
    log_jni_call("RegisterNatives", "clazz=%p, nMethods=%d", clazz, nMethods);
    
    if (methods && nMethods > 0) {
        for (jint i = 0; i < nMethods; i++) {
            log_info("  [%d] name=%s, signature=%s, fnPtr=%p",
                     i,
                     methods[i].name ? methods[i].name : "NULL",
                     methods[i].signature ? methods[i].signature : "NULL",
                     methods[i].fnPtr);
        }
    }
    
    // JSON 로그 - 복잡한 구조체 배열 처리
    const char* arg_names[] = {"env", "clazz", "nMethods", "methods_detail"};
    char env_str[32], clazz_str[32], nMethods_str[32];
    char methods_detail[2048] = "[";
    
    snprintf(env_str, sizeof(env_str), "%p", (void*)env);
    snprintf(clazz_str, sizeof(clazz_str), "%p", (void*)clazz);
    snprintf(nMethods_str, sizeof(nMethods_str), "%d", nMethods);
    
    if (methods && nMethods > 0) {
        for (jint i = 0; i < nMethods; i++) {
            char method_entry[512];
            snprintf(method_entry, sizeof(method_entry),
                     "%s{\"name\":\"%s\",\"signature\":\"%s\",\"fnPtr\":\"%p\"}",
                     i > 0 ? "," : "",
                     methods[i].name ? methods[i].name : "NULL",
                     methods[i].signature ? methods[i].signature : "NULL",
                     methods[i].fnPtr);
            strncat(methods_detail, method_entry, sizeof(methods_detail) - strlen(methods_detail) - 1);
        }
    }
    strncat(methods_detail, "]", sizeof(methods_detail) - strlen(methods_detail) - 1);
    
    const char* arg_values[] = {env_str, clazz_str, nMethods_str, methods_detail};
    log_jni_call_json("RegisterNatives", arg_names, arg_values, 4);
    
    increment_call_count("RegisterNatives");
    
    return JNI_OK;
}

// ============================================
// 기타 JNI 함수들 (최소 로깅)
// ============================================

static jint JNICALL fake_GetVersion(JNIEnv* env) {
    log_jni_call("GetVersion", "");
    return JNI_VERSION_1_6; // Android에서 일반적으로 사용하는 버전
}

static jclass JNICALL fake_DefineClass(
    JNIEnv* env,
    const char* name,
    jobject loader,
    const jbyte* buf,
    jsize len
) {
    log_jni_call("DefineClass", "name=%s, len=%d", name ? name : "NULL", len);
    return (jclass)0x1001;
}

// ... (200개 이상의 함수 계속 구현)
// 각 함수는 동일 패턴:
// 1. log_jni_call()로 호출 기록
// 2. 필요시 인자 상세 로깅
// 3. 안전한 더미 값 반환

// ============================================
// 함수 테이블 초기화
// ============================================

static struct JNINativeInterface_ g_fake_jni_funcs = {
    NULL, NULL, NULL, NULL, // reserved
    
    fake_GetVersion,
    fake_DefineClass,
    fake_FindClass,
    // ... 모든 함수 포인터 설정
    fake_GetMethodID,
    // ...
    fake_RegisterNatives,
    // ... (200+ 항목)
};

// ============================================
// JNIEnv 생성 함수
// ============================================

JNIEnv* create_fake_jnienv(void) {
    static JNIEnv env;
    env.functions = &g_fake_jni_funcs;
    
    log_info("Fake JNIEnv created with full function table");
    
    return &env;
}
```

#### 구현 전략
- **최소 구현**: 빠른 개발을 위해 핵심 20개 함수만 먼저 구현
- **전체 구현**: 안정성을 위해 모든 함수를 stub으로 구현 (스크립트 자동 생성 권장)
- **점진적 확장**: 분석 중 새로운 함수 호출 발견 시 추가

---

### 3. jni_logger.c - 로깅 시스템

#### 역할
- JNI 함수 호출 기록
- 타임스탬프 및 콜 스택 정보
- 통계 정보 수집

#### 구현

```c
#include "jni_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

static FILE* g_log_file = NULL;
static int g_call_count = 0;

// 함수 호출 통계
typedef struct {
    char func_name[128];
    int count;
} CallStat;

static CallStat g_call_stats[500];
static int g_stat_count = 0;

void init_logger(const char* log_path) {
    g_log_file = fopen(log_path, "w");
    if (!g_log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", log_path);
        g_log_file = stdout;
    }
    
    time_t now = time(NULL);
    fprintf(g_log_file, "=== JNI Harness Log ===\n");
    fprintf(g_log_file, "Started at: %s\n", ctime(&now));
    fflush(g_log_file);
}

void log_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    fprintf(g_log_file, "[INFO] ");
    vfprintf(g_log_file, format, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    fprintf(g_log_file, "[ERROR] ");
    vfprintf(g_log_file, format, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    // stderr에도 출력
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    
    va_end(args);
}

void log_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    fprintf(g_log_file, "[WARN] ");
    vfprintf(g_log_file, format, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    va_end(args);
}

void log_jni_call(const char* func_name, const char* format, ...) {
    g_call_count++;
    
    // 타임스탬프
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    fprintf(g_log_file, "[JNI #%04d] [%ld.%06ld] %s(", 
            g_call_count,
            ts.tv_sec,
            ts.tv_nsec / 1000,
            func_name);
    
    if (format && strlen(format) > 0) {
        va_list args;
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
    }
    
    fprintf(g_log_file, ")\n");
    fflush(g_log_file);
}

void increment_call_count(const char* func_name) {
    // 통계 업데이트
    for (int i = 0; i < g_stat_count; i++) {
        if (strcmp(g_call_stats[i].func_name, func_name) == 0) {
            g_call_stats[i].count++;
            return;
        }
    }
    
    // 새 항목 추가
    if (g_stat_count < 500) {
        strncpy(g_call_stats[g_stat_count].func_name, func_name, 127);
        g_call_stats[g_stat_count].count = 1;
        g_stat_count++;
    }
}

void print_log_summary(void) {
    fprintf(g_log_file, "\n=== Call Statistics ===\n");
    fprintf(g_log_file, "Total JNI calls: %d\n", g_call_count);
    fprintf(g_log_file, "Unique functions called: %d\n\n", g_stat_count);
    
    fprintf(g_log_file, "Function Call Counts:\n");
    for (int i = 0; i < g_stat_count; i++) {
        fprintf(g_log_file, "  %-30s : %d\n", 
                g_call_stats[i].func_name,
                g_call_stats[i].count);
    }
    
    fflush(g_log_file);
}

void close_logger(void) {
    if (g_log_file && g_log_file != stdout) {
        fclose(g_log_file);
    }
}
```

---

### 4. 헤더 파일들

#### fake_jni.h
```c
#ifndef FAKE_JNI_H
#define FAKE_JNI_H

#include "jni.h"

// 가짜 JNIEnv 생성 함수
JNIEnv* create_fake_jnienv(void);

#endif // FAKE_JNI_H
```

#### jni_logger.h
```c
#ifndef JNI_LOGGER_H
#define JNI_LOGGER_H

// 로거 초기화/종료
void init_logger(const char* log_path);
void close_logger(void);

// 로깅 함수
void log_info(const char* format, ...);
void log_error(const char* format, ...);
void log_warning(const char* format, ...);
void log_jni_call(const char* func_name, const char* format, ...);

// 통계
void increment_call_count(const char* func_name);
void print_log_summary(void);

#endif // JNI_LOGGER_H
```

#### json_logger.h
```c
#ifndef JSON_LOGGER_H
#define JSON_LOGGER_H

#include <stddef.h>

// JSON 로거 초기화/종료
void init_json_logger(const char* json_path);
void close_json_logger(void);

// JSON 로그 엔트리 작성
void log_jni_call_json(const char* func_name, const char** arg_names, 
                       const char** arg_values, int arg_count);

// 유틸리티 함수
char* escape_json_string(const char* str);
void json_add_string(const char* key, const char* value);
void json_add_number(const char* key, long long value);
void json_add_pointer(const char* key, void* ptr);

#endif // JSON_LOGGER_H
```

---

### 5. json_logger.c - JSON 포맷 로거

#### 역할
- JNI 함수 호출을 JSON 형식으로 저장
- 구조화된 데이터로 분석 도구와 연동 용이
- 실시간 스트리밍 JSON 출력

#### 구현

```c
#include "json_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE* g_json_file = NULL;
static int g_json_call_index = 0;
static int g_first_entry = 1;

void init_json_logger(const char* json_path) {
    g_json_file = fopen(json_path, "w");
    if (!g_json_file) {
        fprintf(stderr, "Failed to open JSON log file: %s\n", json_path);
        return;
    }
    
    // JSON 배열 시작
    fprintf(g_json_file, "{\n");
    fprintf(g_json_file, "  \"log_version\": \"1.0\",\n");
    fprintf(g_json_file, "  \"timestamp\": %ld,\n", time(NULL));
    fprintf(g_json_file, "  \"calls\": [\n");
    fflush(g_json_file);
}

char* escape_json_string(const char* str) {
    if (!str) return strdup("null");
    
    size_t len = strlen(str);
    char* escaped = (char*)malloc(len * 2 + 3); // 최악의 경우 모든 문자 escape + 따옴표
    char* p = escaped;
    
    *p++ = '\"';
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '\"': *p++ = '\\''; *p++ = '\"'; break;
            case '\\': *p++ = '\\''; *p++ = '\\'; break;
            case '\n': *p++ = '\\''; *p++ = 'n'; break;
            case '\r': *p++ = '\\''; *p++ = 'r'; break;
            case '\t': *p++ = '\\''; *p++ = 't'; break;
            default: *p++ = str[i];
        }
    }
    *p++ = '\"';
    *p = '\0';
    
    return escaped;
}

void log_jni_call_json(const char* func_name, const char** arg_names, 
                       const char** arg_values, int arg_count) {
    if (!g_json_file) return;
    
    // 쉼표 처리 (첫 엔트리 제외)
    if (!g_first_entry) {
        fprintf(g_json_file, ",\n");
    }
    g_first_entry = 0;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    fprintf(g_json_file, "    {\n");
    fprintf(g_json_file, "      \"index\": %d,\n", g_json_call_index++);
    fprintf(g_json_file, "      \"timestamp_sec\": %ld,\n", ts.tv_sec);
    fprintf(g_json_file, "      \"timestamp_nsec\": %ld,\n", ts.tv_nsec);
    fprintf(g_json_file, "      \"function\": \"%s\",\n", func_name);
    fprintf(g_json_file, "      \"arguments\": {\n");
    
    for (int i = 0; i < arg_count; i++) {
        char* escaped_value = escape_json_string(arg_values[i]);
        fprintf(g_json_file, "        \"%s\": %s", arg_names[i], escaped_value);
        if (i < arg_count - 1) {
            fprintf(g_json_file, ",\n");
        } else {
            fprintf(g_json_file, "\n");
        }
        free(escaped_value);
    }
    
    fprintf(g_json_file, "      }\n");
    fprintf(g_json_file, "    }");
    fflush(g_json_file);
}

void close_json_logger(void) {
    if (g_json_file) {
        fprintf(g_json_file, "\n  ],\n");
        fprintf(g_json_file, "  \"total_calls\": %d\n", g_json_call_index);
        fprintf(g_json_file, "}\n");
        fclose(g_json_file);
    }
}
```

---

## 🔨 빌드 시스템

### Makefile

```makefile
# 아키텍처 자동 감지
ARCH := $(shell uname -m)

# 컴파일러 설정
CC = gcc
CFLAGS = -Wall -Wextra -g -I./include -I./src
LDFLAGS = -ldl -lpthread

# 아키텍처별 플래그
ifeq ($(ARCH),x86_64)
    CFLAGS += -DARCH_X86_64
    ARCH_SUFFIX = x86_64
else ifeq ($(ARCH),aarch64)
    CFLAGS += -DARCH_ARM64
    ARCH_SUFFIX = arm64
else ifeq ($(ARCH),arm64)
    CFLAGS += -DARCH_ARM64
    ARCH_SUFFIX = arm64
else
    $(error Unsupported architecture: $(ARCH))
endif

# 디렉토리
SRC_DIR = src
BUILD_DIR = build
TARGET_DIR = target
LOG_DIR = logs

# 소스 파일
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/fake_jni.c \
          $(SRC_DIR)/jni_logger.c \
          $(SRC_DIR)/json_logger.c

OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# 실행 파일
TARGET = $(BUILD_DIR)/jni_harness_$(ARCH_SUFFIX)

# 기본 타겟
all: directories $(TARGET) show-info

show-info:
	@echo "==============================================="
	@echo "Build complete for architecture: $(ARCH)"
	@echo "Executable: $(TARGET)"
	@echo "==============================================="
	@echo ""
	@echo "To verify target SO architecture:"
	@echo "  file target/your_library.so"
	@echo ""
	@echo "To run:"
	@echo "  ./$(TARGET) target/your_library.so"
	@echo "==============================================="

# 디렉토리 생성
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(TARGET_DIR)
	@mkdir -p $(LOG_DIR)

# 링크
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# 컴파일
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 아키텍처 확인
check-arch:
	@echo "Current build architecture: $(ARCH)"
	@file $(TARGET) 2>/dev/null || echo "Harness not built yet"
	@echo ""
	@echo "Checking target SO files:"
	@find $(TARGET_DIR) -name "*.so" -exec file {} \; 2>/dev/null || echo "No SO files found"

# 실행 (예시)
run: $(TARGET)
	./$(TARGET) $(TARGET_DIR)/libtarget.so

# 테스트
test: $(TARGET)
	@echo "Running test..."
	./$(TARGET) $(TARGET_DIR)/test_sample.so

# 크로스 컴파일 (x86_64에서 ARM64 빌드)
cross-arm64:
	$(MAKE) CC=aarch64-linux-gnu-gcc ARCH=aarch64

# 크로스 컴파일 (ARM64에서 x86_64 빌드)
cross-x86_64:
	$(MAKE) CC=x86-64-linux-gnu-gcc ARCH=x86_64

# 클린
clean:
	rm -rf $(BUILD_DIR)/*
	rm -rf $(LOG_DIR)/*

.PHONY: all directories run test clean check-arch cross-arm64 cross-x86_64 show-info
```

---

## 🚀 사용 방법

### 빌드

#### 환경별 빌드 가이드

**Linux / macOS:**
```bash
cd AOS_haness_project
make

# 출력 예시:
# ===============================================
# Build complete for architecture: x86_64
# Executable: build/jni_harness_x86_64
# ===============================================
```

**Windows (WSL2):**
```powershell
# 1. WSL2 진입
wsl

# 2. Linux와 동일하게 빌드
cd /mnt/d/Mobil_Live/app_tools/AOS_haness_project
make

# 3. WSL 내에서 실행
./build/jni_harness_x86_64 target/libnative.so
```

**Android (기기에서 직접 빌드 - 드묾):**
```bash
# Termux 앱 설치 후
pkg install clang make
make
```

#### 아키텍처 확인

```bash
# 빌드된 하네스와 타겟 SO의 아키텍처 확인
make check-arch

# 또는 직접 확인
file build/jni_harness_*
file target/*.so
```

출력 예시:
```
build/jni_harness_arm64: ELF 64-bit LSB executable, ARM aarch64
target/libnative.so: ELF 64-bit LSB shared object, ARM aarch64
```

#### 크로스 컴파일

**x86_64 머신에서 ARM64 하네스 빌드:**

```bash
# 1. 크로스 컴파일러 설치 (Ubuntu/Debian)
sudo apt-get install gcc-aarch64-linux-gnu

# 2. 빌드
make cross-arm64

# 3. ARM64 기기로 전송 후 실행
scp build/jni_harness_arm64 user@arm-device:/path/to/
```

**ARM64에서 x86_64 빌드 (드물지만 가능):**

```bash
sudo apt-get install gcc-x86-64-linux-gnu
make cross-x86_64
```

### 실행

#### Android (adb shell)

```bash
# 1. 하네스와 타겟 SO를 기기로 전송
adb push build/jni_harness_arm64 /data/local/tmp/
adb push target/libnative.so /data/local/tmp/

# 2. 실행 권한 부여
adb shell chmod +x /data/local/tmp/jni_harness_arm64

# 3. 실행
adb shell
cd /data/local/tmp
./jni_harness_arm64 libnative.so

# 4. 로그 가져오기
exit
adb pull /data/local/tmp/logs/jni_hook.log ./
adb pull /data/local/tmp/logs/jni_hook.json ./
```

#### ❌ Windows 네이티브 실행 불가

```powershell
# 이렇게 하면 에러 발생
.\build\jni_harness_x86_64.exe .\target\libnative-lib.so
# Error: dlopen not found

# 반드시 WSL2 사용 필요
```

#### ⚠️ 아키텍처 불일치 에러

하네스와 SO의 아키텍처가 맞지 않으면 다음과 같은 에러 발생:

```bash
$ ./build/jni_harness_x86_64 ./target/libnative_arm64.so
[ERROR] dlopen failed: ./target/libnative_arm64.so: 
        cannot open shared object file: wrong ELF class: ELFCLASS64

# 또는
[ERROR] dlopen failed: ./target/libnative_arm64.so: 
        ELF file ABI version invalid
```

**해결 방법:**
1. 타겟 SO의 아키텍처 확인: `file target/libnative.so`
2. 해당 아키텍처로 하네스 재빌드
3. 또는 크로스 컴파일 사용

### 출력 확인

```bash
# 실시간 텍스트 로그 확인
tail -f ./logs/jni_hook.log

# 로그 분석
grep "FindClass" ./logs/jni_hook.log
grep "GetMethodID" ./logs/jni_hook.log

# JSON 로그 확인 (pretty print)
cat ./logs/jni_hook.json | python -m json.tool

# JSON 로그에서 특정 함수만 추출
jq '.calls[] | select(.function == "FindClass")' ./logs/jni_hook.json

# 함수 호출 빈도 분석
jq '[.calls[].function] | group_by(.) | map({function: .[0], count: length})' ./logs/jni_hook.json
```

---

## 📊 예상 출력 예시

### 텍스트 로그 (jni_hook.log)

```
=== JNI Harness Log ===
Started at: Wed Feb 26 10:30:45 2026

[INFO] === JNI Harness Started ===
[INFO] Target SO: ./target/libnative-lib.so
[INFO] SO loaded successfully
[INFO] .init_array functions executed automatically
[INFO] JNI_OnLoad found at 0x7f8a4c001230
[INFO] Calling JNI_OnLoad with fake JavaVM...

[JNI #0001] [1234.567890] GetVersion()
[JNI #0002] [1234.568123] FindClass(name=com/example/myapp/MainActivity)
[JNI #0003] [1234.568234] GetMethodID(clazz=0x1000, name=onCreate, sig=(Landroid/os/Bundle;)V)
[JNI #0004] [1234.568345] FindClass(name=java/lang/String)
[JNI #0005] [1234.568456] GetStaticMethodID(clazz=0x1000, name=valueOf, sig=(I)Ljava/lang/String;)
[JNI #0006] [1234.568567] RegisterNatives(clazz=0x1000, nMethods=3)
  [0] name=nativeInit, signature=()V, fnPtr=0x7f8a4c001500
  [1] name=nativeProcess, signature=(Ljava/lang/String;)I, fnPtr=0x7f8a4c001600
  [2] name=nativeCleanup, signature=()V, fnPtr=0x7f8a4c001700

[INFO] JNI_OnLoad returned version: 0x10006

=== Call Statistics ===
Total JNI calls: 6
Unique functions called: 5

Function Call Counts:
  GetVersion                     : 1
  FindClass                      : 2
  GetMethodID                    : 1
  GetStaticMethodID              : 1
  RegisterNatives                : 1

[INFO] === JNI Harness Finished ===
```

### JSON 로그 (jni_hook.json)

```json
{
  "log_version": "1.0",
  "timestamp": 1740556245,
  "calls": [
    {
      "index": 0,
      "timestamp_sec": 1234,
      "timestamp_nsec": 567890000,
      "function": "GetVersion",
      "arguments": {
        "env": "0x7ffc12345678"
      }
    },
    {
      "index": 1,
      "timestamp_sec": 1234,
      "timestamp_nsec": 568123000,
      "function": "FindClass",
      "arguments": {
        "env": "0x7ffc12345678",
        "name": "com/example/myapp/MainActivity"
      }
    },
    {
      "index": 2,
      "timestamp_sec": 1234,
      "timestamp_nsec": 568234000,
      "function": "GetMethodID",
      "arguments": {
        "env": "0x7ffc12345678",
        "clazz": "0x1000",
        "name": "onCreate",
        "sig": "(Landroid/os/Bundle;)V"
      }
    },
    {
      "index": 3,
      "timestamp_sec": 1234,
      "timestamp_nsec": 568345000,
      "function": "FindClass",
      "arguments": {
        "env": "0x7ffc12345678",
        "name": "java/lang/String"
      }
    },
    {
      "index": 4,
      "timestamp_sec": 1234,
      "timestamp_nsec": 568456000,
      "function": "GetStaticMethodID",
      "arguments": {
        "env": "0x7ffc12345678",
        "clazz": "0x1000",
        "name": "valueOf",
        "sig": "(I)Ljava/lang/String;"
      }
    },
    {
      "index": 5,
      "timestamp_sec": 1234,
      "timestamp_nsec": 568567000,
      "function": "RegisterNatives",
      "arguments": {
        "env": "0x7ffc12345678",
        "clazz": "0x1000",
        "nMethods": "3",
        "methods": [
          {"name": "nativeInit", "signature": "()V", "fnPtr": "0x7f8a4c001500"},
          {"name": "nativeProcess", "signature": "(Ljava/lang/String;)I", "fnPtr": "0x7f8a4c001600"},
          {"name": "nativeCleanup", "signature": "()V", "fnPtr": "0x7f8a4c001700"}
        ]
      }
    }
  ],
  "total_calls": 6
}
```

---

## 🎯 구현 우선순위

### Phase 1: 고급 기능
**목표**: 분석 편의성 향상

추가 기능:
- 실시간 필터링 (특정 함수만 로깅)
- 콜 스택 추적 (backtrace)
- 메모리 덤프 기능
- Interactive 모드 (gdb 스타일)
- 웹 기반 로그 뷰어
- 실시간 JSON 스트리밍

**예상 소요 시간**: 8-12시간

---

## 🛠️ 개발 가이드

### 자동 Stub 생성 스크립트 (Python)

JNI 함수 200개를 수동으로 작성하는 것은 비효율적이므로, 자동 생성 스크립트를 사용:

```python
# generate_jni_stubs.py

JNI_FUNCTIONS = [
    # Format: (return_type, function_name, parameters)
    ("jint", "GetVersion", [("JNIEnv*", "env")]),
    ("jclass", "DefineClass", [
        ("JNIEnv*", "env"),
        ("const char*", "name"),
        ("jobject", "loader"),
        ("const jbyte*", "buf"),
        ("jsize", "len")
    ]),
    ("jclass", "FindClass", [("JNIEnv*", "env"), ("const char*", "name")]),
    # ... 나머지 함수들
]

def generate_stub(ret_type, func_name, params):
    # 파라미터 문자열 생성
    param_str = ", ".join([f"{ptype} {pname}" for ptype, pname in params])
    param_names = ", ".join([pname for _, pname in params[1:]])  # env 제외
    
    # 로깅 포맷 생성
    log_format = ", ".join([f"{pname}=%p" for _, pname in params[1:]])
    
    stub_code = f'''
static {ret_type} JNICALL fake_{func_name}({param_str}) {{
    // 텍스트 로그
    log_jni_call("{func_name}", "{log_format}", {param_names});
    
    // JSON 로그 - 모든 인자 캡처
    const char* arg_names[] = {{{', '.join(['\"' + pname + '\"' for _, pname in params])}}};
    char arg_values[{len(params)}][256];
'''
    
    # 각 인자를 문자열로 변환
    for i, (ptype, pname) in enumerate(params):
        if 'char*' in ptype:
            stub_code += f'    snprintf(arg_values[{i}], 256, "%s", {pname} ? {pname} : "NULL");\n'
        else:
            stub_code += f'    snprintf(arg_values[{i}], 256, "%p", (void*){pname});\n'
    
    stub_code += f'''
    const char* arg_value_ptrs[] = {{{', '.join([f'arg_values[{i}]' for i in range(len(params))])}}};
    log_jni_call_json("{func_name}", arg_names, arg_value_ptrs, {len(params)});
    
    increment_call_count("{func_name}");
'''
    
    # 반환 타입에 따른 더미 값
    if ret_type == "void":
        stub_code += "    // void function\n"
    elif ret_type.startswith("j"):  # jclass, jmethodID 등
        stub_code += f"    return ({ret_type})0x1000;\n"
    else:
        stub_code += "    return NULL;\n"
    
    stub_code += "}\n"
    
    return stub_code

# 전체 stub 코드 생성
with open("src/fake_jni_generated.c", "w") as f:
    f.write("// Auto-generated JNI stubs\n\n")
    for ret_type, func_name, params in JNI_FUNCTIONS:
        f.write(generate_stub(ret_type, func_name, params))
```

### 디버깅 팁

1. **segfault 발생 시**
   ```bash
   gdb ./build/jni_harness
   (gdb) run ./target/libtarget.so
   (gdb) bt  # 백트레이스 확인
   ```

2. **누락된 JNI 함수 찾기**
   - segfault 위치에서 어떤 함수 포인터가 NULL인지 확인
   - 해당 함수 stub 추가

3. **로그가 너무 많을 때**
   - 필터링 옵션 추가
   - grep으로 후처리

---

## ⚠️ 주의사항 및 제약사항

### 동작 제약
1. **실제 Java 객체 없음**: JNI 함수가 반환하는 값은 모두 더미
2. **Java 코드 실행 불가**: VM이 없으므로 실제 Java 메서드 호출 불가
3. **JNI 에러 처리**: Target SO가 반환값을 검증하면 비정상 종료 가능
4. **아키텍처 일치 필수**: 하네스와 타겟 SO는 반드시 동일 아키텍처여야 함

### 분석 범위
- **가능**: JNI 함수 호출 패턴, 인자, 호출 빈도
- **불가능**: 실제 Java 객체 동작, 런타임 값 변화

### 보안 고려사항
- 분석 대상 SO가 악성코드일 경우 실행 주의
- 샌드박스 환경 사용 권장
- 네트워크 격리 환경에서 실행

---

## 🧪 테스트 전략

### 단위 테스트

간단한 테스트 SO 생성:

```c
// test_simple.c
#include <jni.h>
#include <stdio.h>

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    printf("Test SO: JNI_OnLoad called\n");
    
    JNIEnv* env;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    
    // 테스트 호출
    (*env)->FindClass(env, "com/example/Test");
    (*env)->GetVersion(env);
    
    return JNI_VERSION_1_6;
}
```

빌드:
```bash
gcc -shared -fPIC -o test_simple.so test_simple.c
```

### 검증 항목

- [ ] SO 로드 성공
- [ ] .init_array 실행 확인
- [ ] JNI_OnLoad 호출 성공
- [ ] FindClass 인자 로깅 확인
- [ ] GetMethodID 인자 로깅 확인
- [ ] 로그 파일 생성 확인
- [ ] 통계 출력 정확성

---

## 📈 확장 가능성

### 향후 개발 아이디어

1. **Frida 통합**: 실제 Android 기기에서 동일 후킹
2. **IDA Pro 플러그인**: 정적 분석과 연동
3. **네트워크 모니터링**: JNI를 통한 네트워크 호출 추적
4. **자동 리포트 생성**: HTML/PDF 분석 리포트
5. **GUI 뷰어**: 실시간 JNI 호출 시각화
6. **JSON 스키마 검증**: 로그 데이터 무결성 확인
7. **Elasticsearch 통합**: 대용량 로그 검색 및 분석
8. **GraphQL API**: 로그 데이터 쿼리 인터페이스

---

## 📚 참고 자료

### 프로젝트 문서
- **[ARCHITECTURE_GUIDE.md](./ARCHITECTURE_GUIDE.md)** - 아키텍처 호환성 및 크로스 컴파일 상세 가이드
- **[JSON_ANALYSIS_GUIDE.md](./JSON_ANALYSIS_GUIDE.md)** - JSON 로그 분석 방법

### JNI 명세
- [Oracle JNI Specification](https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/jniTOC.html)
- [Android JNI Tips](https://developer.android.com/training/articles/perf-jni)

### 동적 분석 도구
- Frida: https://frida.re/
- ltrace: Linux library call tracer
- strace: System call tracer

### ELF 파일 형식 및 아키텍처
- [ELF Format Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [ARM64 ABI Documentation](https://developer.arm.com/documentation/ihi0055/latest)
- [Android NDK ABI Management](https://developer.android.com/ndk/guides/abis)
- [x86_64 System V ABI](https://gitlab.com/x86-psABIs/x86-64-ABI)

---

## 🤝 개발 시작 체크리스트

AI 개발자가 이 문서를 읽고 바로 시작할 수 있도록:

- [ ] 1. 프로젝트 디렉토리 구조 생성
- [ ] 2. 타겟 환경 결정 (ARM64 또는 x86_64)
- [ ] 3. 필요시 크로스 컴파일 환경 구축
- [ ] 4. Android JNI 헤더 파일(`jni.h`) 준비
- [ ] 3. `jni_logger.h` 및 `jni_logger.c` 구현
- [ ] 4. `json_logger.h` 및 `json_logger.c` 구현
- [ ] 5. `fake_jni.h` 작성
- [ ] 6. `fake_jni.c` MVP 버전 구현 (20개 핵심 함수 + JSON 로깅)
- [ ] 7. `main.c` 구현 (텍스트 + JSON 로거 초기화)
- [ ] 8. `Makefile` 작성
- [ ] 9. 테스트 SO 파일로 검증
- [ ] 10. JSON 출력 형식 확인 및 검증
- [ ] 11. 실제 Android SO로 테스트
- [ ] 12. Phase 3 진행: 전체 JNI 함수 stub 구현

---

## 💡 AI 개발자를 위한 실행 계획

### 즉시 수행할 작업

1. **파일 생성 순서**
   ```
   1) include/jni.h (Android NDK에서 복사)
   2) src/jni_logger.h
   3) src/jni_logger.c
   4) src/fake_jni.h
   5) src/fake_jni.c (MVP: 핵심 20개 함수)
   6) src/main.c
   7) Makefile
   8) README.md (사용자 가이드)
   ```

2. **첫 번째 테스트**
   - 간단한 테스트 SO 생성
   - 하네스 실행 및 로그 확인
   - FindClass, GetMethodID 호출 검증

3. **점진적 확장**
   - 더 많은 JNI 함수 stub 추가
   - 실제 Android SO로 테스트
   - 에러 발생 시 누락된 함수 보완

### 코드 작성 우선순위

**High Priority (Phase 1)**:
- main.c 하네스 로더
- fake_jni.c 핵심 20개 함수
- jni_logger.c 기본 텍스트 로깅

**High Priority (Phase 2)**:
- json_logger.c JSON 포맷 로깅
- 모든 JNI 함수에 JSON 로깅 통합
- 인자 직렬화 및 escape 처리

**Medium Priority (Phase 3)**:
- fake_jni.c 전체 함수 구현 (200+ 함수)
- 자동 생성 스크립트
- 고급 로깅 (통계, 필터링)

**Low Priority (Phase 4)**:
- Interactive 모드
- 웹 기반 로그 뷰어
- 실시간 스트리밍

---

## 결론

이 문서는 Android SO 파일의 JNI 함수 호출을 후킹하여 분석하는 하네스 프로그램의 완전한 개발 명세서입니다. AI 개발자는 이 문서를 참고하여 즉시 구현을 시작할 수 있으며, Phase 1 MVP부터 시작하여 점진적으로 기능을 확장할 수 있습니다.

### 핵심 원리

**JNIEnv 함수 테이블을 stub으로 구현**하여 실제 Android VM 없이도 JNI 함수 호출을 가로채고 인자를 로깅하는 것입니다.

### 주요 특징

1. **이중 로깅**: 텍스트 + JSON 형식으로 실시간 모니터링과 데이터 분석 모두 지원
2. **멀티 아키텍처**: ARM64와 x86_64 모두 지원하여 실제 기기와 에뮬레이터 분석 가능
3. **완전한 인자 캡처**: 모든 JNI 함수의 모든 인자를 상세히 기록
4. **확장 가능**: Phase별로 점진적 기능 확장 가능

### ⚠️ 시작 전 필수 확인

1. **타겟 SO의 아키텍처 확인**: `file target/your_library.so`
2. **적절한 하네스 빌드**: ARM64 SO → ARM64 하네스 필요
3. **크로스 컴파일 환경**: 필요시 [ARCHITECTURE_GUIDE.md](./ARCHITECTURE_GUIDE.md) 참조
