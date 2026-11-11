# Windows 64비트 환경 설정 및 빌드 가이드

이 문서는 Win64 호스트에서 repsp 런타임 라이브러리와 검증 프로브를 빌드하고 테스트하는 절차를 설명한다.

---

## 1. 사전 요구사항 (Prerequisites)

* **운영체제**: Windows 10 64비트 이상
* **컴파일러**: Visual Studio 2022 이상 (MSVC v143+ 또는 v144+, "C++를 사용한 데스크톱 개발" 워크로드 설치)
* **빌드 도구**:
  * CMake 3.25 이상
  * Git (서브모듈 재귀 지원)

---

## 2. 저장소 준비 (Repository Setup)

서브모듈(`third_party/SDL`)을 포함하여 저장소를 클론한다:

```powershell
git clone --recursive https://github.com/reexec/repsp.git
cd repsp
```

이미 클론된 저장소라면 서브모듈을 갱신한다:

```powershell
git submodule update --init --recursive
```

---

## 3. 빌드 및 테스트 절차 (Build and Test)

### 3.1. PowerShell 스크립트 사용 (권장)

```powershell
# Debug 빌드 및 CTest 실행
.\scripts\build_win64.ps1 -Config Debug

# Release 빌드 및 CTest 실행
.\scripts\build_win64.ps1 -Config Release
```

### 3.2. 배치 파일 사용

```cmd
scripts\build_win64.bat Debug
scripts\build_win64.bat Release
```

### 3.3. CMake Presets 직접 사용

```powershell
# Configure
cmake --preset win64-debug

# Build
cmake --build build/win64-debug --config Debug

# Test
ctest --test-dir build/win64-debug -C Debug --output-on-failure
```

---

## 4. 검증 결과 확인 (Verification)

빌드가 완료되면 CTest가 5개의 검증 프로브를 실행하며, 모두 `Passed` 상태가 되어야 한다:

1. `memory_probe`: 게스트 메모리 윈도우, 가상 주소 미러 접기, 스칼라/블록 읽기·쓰기 검증
2. `module_probe`: `PARAM.SFO` 바이너리 메타데이터 파싱 및 비정상 입력 거부 검증
3. `nid_probe`: NID 계산 일치, HLE 디스패치 및 미구현 호출 카운트 통계 검증
4. `recomp_contract_probe`: 가짜 CPU 어댑터를 통한 인자/반환 규약, NID 디스패치, 컨텍스트 저장·복원 검증
5. `platform_probe`: 가상 메모리 보호 실행(`GuardedRun`), 호스트 스레드, SDL3 더미 드라이버 연동 검증

---

# Win64 Setup and Build Guide (English)

This document describes how to set up the environment, build repsp, and run CTest verification probes on Windows 64-bit.

## 1. Prerequisites
* Windows 10/11 64-bit
* Visual Studio 2022+ with Desktop C++ workload
* CMake 3.25+ and Git

## 2. Repository Setup
Clone with `--recursive` to include the SDL submodule.

## 3. Building and Testing
Use `.\scripts\build_win64.ps1 -Config Debug` or CMake presets (`cmake --preset win64-debug`).

## 4. Verification
All 5 probes (`memory_probe`, `module_probe`, `nid_probe`, `recomp_contract_probe`, `platform_probe`) must pass.
