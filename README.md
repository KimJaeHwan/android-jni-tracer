# Android JNI Hooking Harness

Android `.so` 파일의 JNI 함수 호출을 후킹하여 분석하는 동적 분석 도구

**Windows에서 빌드 → Android에서 실행**

## ⚠️ 중요: 실행 환경

**이 프로그램은 Android 기기에서 실행되는 네이티브 바이너리입니다.**

✅ **지원 환경:**
- **Windows + Android NDK** - 빌드 환경 (Android 타겟 바이너리 생성)
- **Android OS** - 실행 환경 (실제 기기 또는 에뮬레이터)

📌 **빌드 프로세스:**
1. Windows에서 Android NDK로 ARM64/x86_64 바이너리 빌드
2. adb를 통해 Android 기기로 전송
3. Android 기기에서 실행

❌ **직접 실행 불가:**
- Windows 네이티브 환경 (빌드만 가능, 실행 불가)
- Linux/macOS 데스크톱 (Android 전용 바이너리)

## 🎯 Quick Start

**Windows에서 빌드 → Android에서 실행**

### 1단계: Windows에서 빌드

**필수 요구사항:**
- Android Studio + NDK 설치
- PowerShell 실행 권한

```powershell
# Android Studio에서 NDK 설치 (한 번만)
#    Tools → SDK Manager → SDK Tools → NDK (Side by side) 체크

# ARM64 빌드 (실제 Android 기기용)
.\build-android.ps1 -Architecture arm64

# x86_64 빌드 (Android 에뮬레이터용)
.\build-android.ps1 -Architecture x86_64

# 빌드 결과
#    build\jni_harness_arm64_android
#    build\jni_harness_x86_64_android
```

### 2단계: Android 기기로 전송 및 실행

```powershell
# Android 기기로 전송
adb push build\jni_harness_arm64_android /data/local/tmp/
adb push target\*.so /data/local/tmp/

# 실행 준비
adb shell "cd /data/local/tmp && mkdir -p logs && chmod +x jni_harness_arm64_android"

# 실행
adb shell "cd /data/local/tmp && LD_LIBRARY_PATH=. ./jni_harness_arm64_android ./libengine.so"
```

### 3단계: 로그 다운로드 및 분석

```powershell
# 로그 다운로드
adb pull /data/local/tmp/logs/ logs/

# 텍스트 로그 확인
type logs\jni_hook.log

# JSON 로그 분석 (jq 필요)
cat logs\jni_hook.json | jq .
```

## 🔧 빌드 요구사항

### Windows 환경
- **Android Studio** 설치
- **NDK (Side by side)** 설치
  - Android Studio → Tools → SDK Manager → SDK Tools 탭
  - "NDK (Side by side)" 체크 후 Apply
- PowerShell 실행 권한 필요
- **adb (Android Debug Bridge)** - Android Platform Tools에 포함

## ⚙️ 아키텍처 지원

이 도구는 **ARM64 (aarch64)** 와 **x86_64** 아키텍처를 모두 지원합니다.

**중요**: 하네스 프로그램과 타겟 SO 파일의 아키텍처가 반드시 일치해야 합니다.

| 하네스 아키텍처 | 타겟 SO 아키텍처 | 동작 여부 |
|----------------|-----------------|----------|
| ARM64          | ARM64           | ✅ 가능   |
| x86_64         | x86_64          | ✅ 가능   |
| ARM64          | x86_64          | ❌ 불가능 |
| x86_64         | ARM64           | ❌ 불가능 |

## 📖 상세 문서

- **[PROJECT_SPECIFICATION.md](./PROJECT_SPECIFICATION.md)** - 완전한 개발 명세서 (AI 개발자용)
- **[JSON_ANALYSIS_GUIDE.md](./JSON_ANALYSIS_GUIDE.md)** - JSON 로그 분석 방법

## ✨ 주요 기능

- **JNI 함수 후킹**: FindClass, GetMethodID 등 모든 JNI 함수 호출 캡처
- **상세 인자 모니터링**: 함수 호출 시 모든 인자값 완벽 캡처
- **이중 로깅 시스템**:
  - 텍스트 로그: 실시간 모니터링용 가독성 높은 포맷
  - JSON 로그: 분석 도구 연동을 위한 구조화된 데이터
- **통계 분석**: 함수별 호출 빈도 및 패턴 분석
- **VM 불필요**: 실제 Android VM 없이 독립 실행

## 📂 프로젝트 구조

```
AOS_haness_project/
├── PROJECT_SPECIFICATION.md  ← AI 개발자용 완전한 명세서
├── README.md                 ← 본 파일
├── build-android.ps1         ← Windows용 Android NDK 빌드 스크립트
├── .gitignore                ← Git 제외 파일 목록
├── src/                      ← 소스 코드
│   ├── main.c               ← 메인 하네스 로더
│   ├── fake_jni.c           ← 230+ JNI 함수 stub 구현
│   ├── jni_logger.c         ← 텍스트 로깅 시스템
│   └── json_logger.c        ← JSON 로깅 시스템
├── include/                  ← 헤더 파일
│   └── jni.h                ← Android JNI 헤더
├── build/                    ← 빌드 결과물 (gitignore)
├── target/                   ← 분석 대상 SO 파일 (gitignore)
└── logs/                     ← 로그 출력 (gitignore)
```

## 🚀 개발 시작

AI 개발자는 `PROJECT_SPECIFICATION.md`를 읽고 즉시 구현을 시작할 수 있습니다.

### 빌드 및 분석 워크플로우

1. **Windows에서 빌드**
   ```powershell
   # ARM64 빌드 (실제 Android 기기용)
   .\build-android.ps1 -Architecture arm64
   
   # x86_64 빌드 (Android 에뮬레이터용)
   .\build-android.ps1 -Architecture x86_64
   ```

2. **Android 기기로 전송**
   ```powershell
   adb push build\jni_harness_arm64_android /data/local/tmp/
   adb push target\your_lib.so /data/local/tmp/
   ```

3. **Android 기기에서 실행**
   ```powershell
   adb shell "cd /data/local/tmp && chmod +x jni_harness_arm64_android"
   adb shell "cd /data/local/tmp && LD_LIBRARY_PATH=. ./jni_harness_arm64_android ./your_lib.so"
   ```

4. **로그 분석**
   ```powershell
   adb pull /data/local/tmp/logs/ logs/
   # logs/jni_hook.log - 텍스트 형식
   # logs/jni_hook.json - JSON 형식
   ```

### 아키텍처 선택 가이드

1. **실제 Android 기기 분석** → ARM64 빌드 필요 (`-Architecture arm64`)
2. **Android 에뮬레이터 분석** → x86_64 빌드 사용 (`-Architecture x86_64`)

### 개발 단계 (AI 참고용)
- 핵심 20개 JNI 함수 후킹
- 기본 로깅 시스템
- 예상 소요: 2-3시간

### Phase 2: JSON 로깅 추가
- 구조화된 데이터 출력
- 예상 소요: 2-3시간

### Phase 3: 완전한 구현
- 200+ 모든 JNI 함수 stub
- 통계 및 분석 기능
- 예상 소요: 4-6시간

## ⚠️ 주의사항

1. **아키텍처 확인 필수**
   ```powershell
   # Windows에서 타겟 SO 파일 확인
   # WSL이 있다면
   wsl file target/libnative.so
   
   # Android 기기에서 확인
   adb shell "file /data/local/tmp/libnative.so"
   # 출력 예: ELF 64-bit LSB shared object, ARM aarch64 ...
   ```

2. **dlopen 에러 발생 시**
   - 아키텍처 불일치가 가장 흔한 원인
   - 의존 라이브러리 누락 시 `LD_LIBRARY_PATH` 설정 필요
   - 모든 의존 .so 파일을 같은 디렉토리에 업로드

3. **Android 기기에서 실행 시**
   - `chmod +x` 로 실행 권한 부여 필수
   - `/data/local/tmp/` 디렉토리 사용 권장
   - SELinux 권한 문제 발생 시 `setenforce 0` (루팅 필요)

## 📝 License

이 프로젝트는 분석 및 연구 목적으로 제작되었습니다.
=======
# Android Jni Harness Tracer



## Getting started

To make it easy for you to get started with GitLab, here's a list of recommended next steps.

Already a pro? Just edit this README.md and make it your own. Want to make it easy? [Use the template at the bottom](#editing-this-readme)!

## Add your files

- [ ] [Create](https://docs.gitlab.com/ee/user/project/repository/web_editor.html#create-a-file) or [upload](https://docs.gitlab.com/ee/user/project/repository/web_editor.html#upload-a-file) files
- [ ] [Add files using the command line](https://docs.gitlab.com/topics/git/add_files/#add-files-to-a-git-repository) or push an existing Git repository with the following command:

```
cd existing_repo
git remote add origin https://gitlab.nexon.com/ghrt/rnd/android-jni-harness-tracer.git
git branch -M main
git push -uf origin main
```

## Integrate with your tools

- [ ] [Set up project integrations](https://gitlab.nexon.com/ghrt/rnd/android-jni-harness-tracer/-/settings/integrations)

## Collaborate with your team

- [ ] [Invite team members and collaborators](https://docs.gitlab.com/ee/user/project/members/)
- [ ] [Create a new merge request](https://docs.gitlab.com/ee/user/project/merge_requests/creating_merge_requests.html)
- [ ] [Automatically close issues from merge requests](https://docs.gitlab.com/ee/user/project/issues/managing_issues.html#closing-issues-automatically)
- [ ] [Enable merge request approvals](https://docs.gitlab.com/ee/user/project/merge_requests/approvals/)
- [ ] [Set auto-merge](https://docs.gitlab.com/user/project/merge_requests/auto_merge/)

## Test and Deploy

Use the built-in continuous integration in GitLab.

- [ ] [Get started with GitLab CI/CD](https://docs.gitlab.com/ee/ci/quick_start/)
- [ ] [Analyze your code for known vulnerabilities with Static Application Security Testing (SAST)](https://docs.gitlab.com/ee/user/application_security/sast/)
- [ ] [Deploy to Kubernetes, Amazon EC2, or Amazon ECS using Auto Deploy](https://docs.gitlab.com/ee/topics/autodevops/requirements.html)
- [ ] [Use pull-based deployments for improved Kubernetes management](https://docs.gitlab.com/ee/user/clusters/agent/)
- [ ] [Set up protected environments](https://docs.gitlab.com/ee/ci/environments/protected_environments.html)

***

# Editing this README

When you're ready to make this README your own, just edit this file and use the handy template below (or feel free to structure it however you want - this is just a starting point!). Thanks to [makeareadme.com](https://www.makeareadme.com/) for this template.

## Suggestions for a good README

Every project is different, so consider which of these sections apply to yours. The sections used in the template are suggestions for most open source projects. Also keep in mind that while a README can be too long and detailed, too long is better than too short. If you think your README is too long, consider utilizing another form of documentation rather than cutting out information.

## Name
Choose a self-explaining name for your project.

## Description
Let people know what your project can do specifically. Provide context and add a link to any reference visitors might be unfamiliar with. A list of Features or a Background subsection can also be added here. If there are alternatives to your project, this is a good place to list differentiating factors.

## Badges
On some READMEs, you may see small images that convey metadata, such as whether or not all the tests are passing for the project. You can use Shields to add some to your README. Many services also have instructions for adding a badge.

## Visuals
Depending on what you are making, it can be a good idea to include screenshots or even a video (you'll frequently see GIFs rather than actual videos). Tools like ttygif can help, but check out Asciinema for a more sophisticated method.

## Installation
Within a particular ecosystem, there may be a common way of installing things, such as using Yarn, NuGet, or Homebrew. However, consider the possibility that whoever is reading your README is a novice and would like more guidance. Listing specific steps helps remove ambiguity and gets people to using your project as quickly as possible. If it only runs in a specific context like a particular programming language version or operating system or has dependencies that have to be installed manually, also add a Requirements subsection.

## Usage
Use examples liberally, and show the expected output if you can. It's helpful to have inline the smallest example of usage that you can demonstrate, while providing links to more sophisticated examples if they are too long to reasonably include in the README.

## Support
Tell people where they can go to for help. It can be any combination of an issue tracker, a chat room, an email address, etc.

## Roadmap
If you have ideas for releases in the future, it is a good idea to list them in the README.

## Contributing
State if you are open to contributions and what your requirements are for accepting them.

For people who want to make changes to your project, it's helpful to have some documentation on how to get started. Perhaps there is a script that they should run or some environment variables that they need to set. Make these steps explicit. These instructions could also be useful to your future self.

You can also document commands to lint the code or run tests. These steps help to ensure high code quality and reduce the likelihood that the changes inadvertently break something. Having instructions for running tests is especially helpful if it requires external setup, such as starting a Selenium server for testing in a browser.

## Authors and acknowledgment
Show your appreciation to those who have contributed to the project.

## License
For open source projects, say how it is licensed.

## Project status
If you have run out of energy or time for your project, put a note at the top of the README saying that development has slowed down or stopped completely. Someone may choose to fork your project or volunteer to step in as a maintainer or owner, allowing your project to keep going. You can also make an explicit request for maintainers.
>>>>>>> b84a7407ab81eb85d43b0b5a71cb98e6a5aee776
