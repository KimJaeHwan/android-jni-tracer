# android-jni-tracer MCP Setup Guide

이 문서는 `android-jni-tracer` MCP 서버를 Codex, VS Code 계열 MCP client,
Claude Desktop, Cursor 같은 agent/client에서 연결하는 방법을 정리한다.

## 1. 핵심 개념

MCP는 모델이 직접 서버에 접속하는 구조가 아니다.

```text
LLM model
  <-> MCP client / agent
      <-> android-jni-tracer MCP server
          <-> run store / adb / Android device
```

따라서 `android-jni-tracer` 입장에서 중요한 것은 "어떤 모델인가"보다
"어떤 MCP client가 이 서버를 어떻게 실행하고 tool을 모델에게 노출하는가"다.

모델은 GPT, Claude, Gemini, local model 중 무엇이든 가능하다. MCP 서버는
모델 이름을 알지 못하고, client가 보내는 MCP tool call만 처리한다.

## 2. 모델 선택은 어디서 하나

모델 선택은 MCP 서버 설정이 아니라 MCP client 설정이다.

예를 들어:

- Codex를 쓰면 Codex 설정에서 모델을 선택한다.
- VS Code extension을 쓰면 해당 extension 설정에서 모델을 선택한다.
- Claude Desktop을 쓰면 Claude Desktop이 사용하는 모델이 tool을 호출한다.
- Cursor, Continue, Roo Code, Cline 같은 도구도 각 client 설정에서 모델을 고른다.

`android-jni-tracer` MCP 서버는 다음 능력이 좋은 모델과 잘 맞는다.

- tool call을 안정적으로 사용하는 모델
- JSON/log 구조를 잘 읽는 모델
- native/JNI/Android 분석 문맥을 이해하는 모델
- 여러 run의 diff를 보고 가설을 세울 수 있는 모델

즉, "MCP 서버용 모델"을 따로 고르는 것이 아니라, agent/client에서 좋은
분석 모델을 선택하면 된다.

## 3. 서버를 직접 켜야 하나

보통은 직접 장기 실행하지 않는다.

대부분의 MCP client는 설정에 적힌 `command`와 `args`로 MCP 서버 프로세스를
직접 실행한다. `android-jni-tracer` MCP 서버도 기본적으로 stdio transport를
사용하므로, client가 프로세스를 띄우고 표준입출력으로 통신한다.

수동 실행은 디버깅용이다.

```bash
jni-tracer mcp serve --runs-root runs
```

이 명령은 일반 CLI처럼 결과를 출력하고 종료하는 프로그램이 아니다. MCP client의
요청을 기다리는 서버이므로 터미널에서 멈춰 있는 것처럼 보이는 것이 정상이다.

## 4. Read-only와 Execution 모드

### 4.1 Read-only MCP

기본 MCP 서버는 기존 run store만 읽는다.

```bash
jni-tracer mcp serve --runs-root runs
```

이 모드에서는 adb나 harness를 실행하지 않는다. 이미 생성된
`runs/<run_id>/logs/jni_hook.json`, `summary.json`, `manifest.json`을 읽어
LLM에게 분석용 tool로 제공한다.

제공 tool:

- `list_runs`
- `get_run`
- `get_summary`
- `get_calls`
- `get_natives`
- `get_classes`
- `diff_runs`
- `validate_mock_config`

### 4.2 Execution MCP

하네스를 MCP tool에서 직접 실행하려면 `--allow-execute`를 켜야 한다.

```bash
jni-tracer mcp serve \
  --runs-root runs \
  --allow-execute \
  --harness build/jni_harness_arm64_android \
  --libs-dir target/libs/arm64-v8a \
  --allowed-so-dir target/libs/arm64-v8a \
  --timeout-sec 30
```

추가 tool:

- `run_harness`
- `run_invoke_plan`
- `rerun_with_mock`

Execution mode에서는 MCP client가 tool call을 보내면 서버가 adb로 하네스를
실행하고, 새 run을 만든 뒤, 결과를 다시 MCP 응답으로 반환한다.

주의할 점은 execution tool의 `adb` 호출이 MCP 서버 프로세스 안에서 다시
일어난다는 것이다. Codex나 다른 agent가 sandbox 안에서 MCP 서버를 띄우는 경우,
read-only tool은 정상이어도 execution tool은 adb daemon/socket 권한 때문에 막힐
수 있다. 이 경우 MCP 서버를 adb 사용이 허용된 환경에서 실행하거나, 해당 client의
권한 승인 흐름을 통해 execution 서버 프로세스 자체에 adb 실행 권한을 줘야 한다.

### 4.3 하네스가 먼저 실행되어야 읽을 수 있는 문제

맞다. 아직 run이 없다면 read-only MCP는 분석할 데이터가 없다.

두 가지 운영 방식이 있다.

1. CLI로 먼저 run을 만든 뒤 read-only MCP로 분석한다.
2. Execution MCP를 켜고 `run_harness`, `run_invoke_plan`, `rerun_with_mock`로
   MCP client가 직접 run을 만들게 한다.

권장 방식은 평소에는 read-only MCP를 쓰고, 실제 디바이스 실행이 필요할 때만
execution MCP를 별도로 켜는 것이다.

## 5. 로컬 설치

MCP client에서 실행할 수 있도록 repo 안에 virtualenv를 만드는 방식을 권장한다.

```bash
cd /absolute/path/to/android-jni-tracer
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install setuptools
python3 -m pip install -e python --no-build-isolation
.venv/bin/jni-tracer --help
.venv/bin/jni-tracer mcp serve --help
```

일반 CLI 분석만 할 때는 `PYTHONPATH=python python3 -m jni_tracer ...` 방식도
가능하지만, MCP client 설정에는 `.venv/bin/jni-tracer` 같은 고정 실행 파일
경로를 넣는 편이 안정적이다.

## 6. Codex 연결 예시

Codex는 MCP 서버 설정을 `~/.codex/config.toml`에 추가하는 방식으로 사용할 수
있다. 현재 repo처럼 project가 trusted인 상태에서 서버를 등록하면 Codex가 tool을
사용할 수 있다.

Read-only 서버 예시:

```toml
[mcp_servers.jni-tracer]
command = "/absolute/path/to/android-jni-tracer/.venv/bin/jni-tracer"
args = [
  "mcp",
  "serve",
  "--runs-root",
  "/absolute/path/to/android-jni-tracer/runs",
]
cwd = "/absolute/path/to/android-jni-tracer"
```

Execution 서버 예시:

```toml
[mcp_servers.jni-tracer-exec]
command = "/absolute/path/to/android-jni-tracer/.venv/bin/jni-tracer"
args = [
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
  "30",
]
cwd = "/absolute/path/to/android-jni-tracer"
```

Codex 설정 파일은 사용자 홈 디렉터리 아래에 있으므로, 자동으로 수정하려면 별도
승인이 필요할 수 있다. 설정을 바꾼 뒤에는 Codex session을 다시 시작하는 편이
가장 확실하다.

## 7. VS Code 계열 client 연결

VS Code 자체가 MCP 서버를 항상 직접 제공하는 것은 아니다. 보통은 VS Code 안에서
동작하는 MCP 지원 extension 또는 agent가 서버를 실행한다.

대표적인 형태:

- Roo Code / Cline 계열: extension의 MCP server 설정에 command/args 등록
- Continue 계열: extension 설정에 MCP server 등록
- Cursor 계열: Cursor의 MCP 설정에 server 등록

client마다 설정 파일 이름과 위치는 다르지만 핵심 구조는 거의 같다.

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

## 8. Claude Desktop 계열 설정 형태

Claude Desktop도 보통 `mcpServers` 형태의 JSON 설정을 사용한다. 경로와 파일명은
OS와 client 버전에 따라 다르므로, 실제 설정 파일 위치는 사용하는 client 문서를
확인해야 한다.

핵심은 read-only 또는 execution 중 하나를 골라 command/args를 등록하는 것이다.

## 9. Local LLM 연결

로컬 LLM에 연결하기 위해 별도의 모델을 개발할 필요는 없다. 필요한 것은 로컬
모델과 MCP 서버 사이에서 tool call을 중계하는 MCP 지원 client 또는 agent다.

구조:

```text
local LLM model
  <-> local MCP client / agent runtime
      <-> android-jni-tracer MCP server
          <-> runs / adb / harness / Android device
```

예시 구성:

- Ollama / LM Studio / llama.cpp / vLLM 등으로 로컬 모델 실행
- Roo Code, Cline, Continue, Cursor 같은 MCP 지원 client에서 모델 선택
- 같은 client 설정에 `jni-tracer` MCP server 등록

중요한 점:

- 모델은 보통 텍스트와 tool call 의도를 생성한다.
- 실제 MCP tool 실행은 client/agent runtime이 담당한다.
- `android-jni-tracer` MCP 서버는 로컬 모델인지 원격 모델인지 알지 못한다.
- 작은 로컬 모델은 단일 tool 조회는 잘해도, 여러 tool을 연쇄적으로 쓰는 분석
  루프에서는 흔들릴 수 있다.

권장 평가 순서:

1. read-only MCP만 연결해 `list_runs`, `get_summary`, `get_natives` 품질을 본다.
2. `get_calls`, `diff_runs`까지 이어지는 다단계 분석을 시킨다.
3. 충분히 안정적이면 execution MCP를 별도로 켠다.
4. `run_invoke_plan`, `rerun_with_mock` 같은 실험 tool을 제한적으로 허용한다.

직접 agent를 만들 수도 있다. 이 경우 Python 등으로 local model API와 MCP client를
묶어 tool schema 제공, tool call JSON 파싱, MCP `call_tool`, 결과 재주입 루프를
구현하면 된다. 다만 재시도, context 관리, 안전장치까지 직접 설계해야 하므로
초기 운영은 기존 MCP 지원 client를 쓰는 편이 현실적이다.

## 10. 권장 운영 정책

- 기본 등록은 read-only MCP로 한다.
- execution MCP는 디바이스가 연결되어 있고 실제 실행이 필요할 때만 켠다.
- execution MCP의 `--allowed-so-dir`를 분석 대상 디렉터리로 좁힌다.
- `--timeout-sec`를 반드시 둔다.
- Codex sandbox 같은 제한 환경에서는 execution MCP 서버 프로세스가 adb를 실행할
  수 있는 권한으로 떠 있는지 확인한다.
- rooted device에서 실행되는 adb 명령은 신뢰 가능한 workspace에서만 허용한다.
- 분석 결과는 항상 run store에 남기고, 이후 분석은 run id 기준으로 진행한다.

## 11. 연결 후 예시 프롬프트

Read-only:

```text
jni-tracer MCP에서 list_runs로 최근 run을 보여줘.
```

```text
run_id baseline_probe의 get_natives와 get_calls(function="InvokeNative")를 보고
등록된 native entry와 실제 invoke 결과를 요약해줘.
```

Execution:

```text
jni-tracer-exec MCP의 run_invoke_plan으로 libtarget.so를 실행하고,
suspend()I native entry 호출 결과까지 분석해줘.
```

```text
기존 run을 기준으로 rerun_with_mock을 실행해서 mock이 JNI call flow에 미친 차이를 diff로 설명해줘.
```

## 12. 현재 구현 범위

현재 MCP 서버는 FastMCP Python SDK 기반 stdio 서버다.

지원:

- read-only run 조회
- JSON summary/calls/natives/classes 조회
- run diff
- mock config 검증
- opt-in 하네스 실행
- invoke plan 실행
- mock 기반 재실행과 diff

아직 기본 제공하지 않는 것:

- 장기 실행 HTTP/SSE MCP daemon
- IDA Pro MCP와의 직접 orchestration
- target APK 설치/실행 자동화
- frida 기반 live app hook

따라서 지금 단계의 MCP는 "JNI harness 실행과 run artifact 분석을 LLM tool로
제공하는 서버"로 보는 것이 정확하다.
