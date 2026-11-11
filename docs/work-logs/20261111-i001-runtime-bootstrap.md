# 작업 로그: 런타임 부트스트랩 (issue #1)

* 작업 브랜치: `i001-runtime-bootstrap`
* 설계: [`docs/design/20261111-i001-runtime-bootstrap.md`](../design/20261111-i001-runtime-bootstrap.md)
* 작업 지시: [`docs/work-orders/20261111-i001-runtime-bootstrap.md`](../work-orders/20261111-i001-runtime-bootstrap.md)

---

## 1. 수행한 작업 (Summary of Work)

1. **빌드 골격 구축**:
   * `VERSION` (`0.0.0`) 파일 작성.
   * `cmake/repsp_host_flags.cmake`: C11/C++20 표준 강제, MSVC `/W4 /WX /permissive- /utf-8 /EHsc` 및 `_CRT_SECURE_NO_WARNINGS`, GCC/Clang `-Wall -Wextra -Wpedantic -Werror` 적용 함수 `repsp_apply_host_flags` 작성.
   * `CMakePresets.json`: `win64-debug`, `win64-release`, `linux-x64-debug`, `linux-x64-release` 구성/빌드/테스트 프리셋 정의.
   * 루트 `CMakeLists.txt`: 레이어별 타깃(`repsp_platform`, `repsp_module`, `repsp_runtime`, `repsp_recomp`, `repsp_hle_kernel`)과 공개 별칭(`repsp::*`), `REPSP_BUILD_PROBES` 옵션 정의.
2. **SDL3 서브모듈 추가**:
   * `third_party/SDL`: `release-3.4.x` 브랜치(`ee239da42bd043039fddbad0622645f54bf5a640`) 서브모듈 등록.
   * 기본적으로 독립 실행 파일 산출 및 의존성 격리를 위해 정적 라이브러리(`SDL_STATIC=ON`, `SDL_SHARED=OFF`)로 빌드하도록 설정 (`REPSP_SDL_SHARED` 옵션 제공).
3. **`repsp_platform` 구현**:
   * 가상 메모리 관리(`virtual_memory.h` / `.cpp`): `ReserveAddressSpace`, `CommitMemory`, `DecommitMemory`, `ReleaseAddressSpace`, `ProtectMemory` (Win32 `VirtualAlloc`, Linux `mmap/mprotect`).
   * 보호 실행(`guarded_run.h` / `.cpp`): `GuardedRun` (Win32 SEH `__try/__except`, Linux `sigaction/sigsetjmp`).
   * 호스트 스레드(`host_thread.h` / `.cpp`): `HostThread`, 스레드 이름 설정, CPU 시간 측정.
   * 경로 유틸리티(`path.h` / `.cpp`): 실행 파일 폴더 및 사용자 데이터 폴더 경로 획득.
   * SDL3 연동(`platform.h`, `sdl/sdl_platform.cpp`): `DisplayBackend`, `InputBackend`, `AudioBackend` 및 더미 드라이버 지원.
4. **`repsp_module` 구현**:
   * `PARAM.SFO` 바이너리 파서(`param_sfo.h` / `.cpp`): 헤더, 엔트리 테이블, 키/데이터 테이블 파싱, 문자열(UTF-8) 및 32비트 정수 키 추출, 유효성 검증.
5. **`repsp_runtime` 구현**:
   * `GuestMemory` (`guest_memory.h` / `.cpp`): 160 MiB 호스트 가상 메모리 창 예약, 물리 영역(scratchpad 16 KiB, VRAM 2 MiB, kernel RAM 8 MiB, user RAM 24 MiB) commit, `0x1FFFFFFF` 미러 접기, 리틀엔디언 스칼라 및 블록 접근.
   * `NidRegistry` (`nid_registry.h` / `.cpp`): NID 등록 및 디스패치, 미구현 NID 통계 누적 및 최초 경고 로그, `REPSP_STOP_ON_UNIMPLEMENTED` 지원.
   * SHA-1 (`sha1.h` / `.cpp`): NID 계산용 독립 SHA-1 다이제스트 및 `ComputeNid` 구현.
   * 로그(`log.h` / `.cpp`): 로그 레벨, 포맷 로깅, 커스텀 콜백 지원.
6. **`repsp_recomp` 구현**:
   * CPU 계약(`guest_cpu.h`): `repsp_guest_cpu_ops` (save, restore, get_gpr, set_gpr, call_guest), C용 메모리 접근 함수(`repsp_guest_base`, `repsp_guest_mask`, `repsp_guest_translate`, `repsp_guest_read*`, `repsp_guest_write*`).
   * 호출 규약 도우미(`recomp_helpers.h`): `Arg<T>(n)` ($a0..$a3, $t0..$t3, 스택), `Return(v)`.
   * HLE 호출 진입점(`guest_cpu.cpp`): `repsp_recomp_hle_call`에서 `NidRegistry::Invoke` 호출, 미구현 시 `$v0`에 `0x80020002` 설정.
7. **`src/hle/` 스켈레톤 및 문서**:
   * `repsp_hle_kernel` 진입점 함수 `RegisterHleKernel()` 제공.
   * `src/hle/README.md`: HLE 모듈 경계, 런타임 단방향 의존성, NID 레지스트리 경유 원칙 명시.
8. **검증 프로브 5종 및 CTest 등록**:
   * `memory_probe`: 물리 영역 변환, 미러 접기, 리틀엔디언 읽기/쓰기, 블록 복사, 유효 주소 검증.
   * `module_probe`: 합성 SFO 파싱 및 잘못된 입력 거부 검증.
   * `nid_probe`: NID=SHA-1 앞 4바이트(LE) 일치, 핸들러 디스패치, 미구현 카운트 통계 검증.
   * `recomp_contract_probe`: 가짜 어댑터 등록, 인자 0..9 및 반환, 미구현 NID의 $v0 반환값, 컨텍스트 저장/복원, C 메모리 함수 검증.
   * `platform_probe`: 주소 공간 예약/commit, 보호 실행의 세그폴트/접근위반 포착, 스레드, SDL3 더미 초기화 검증.
9. **빌드 스크립트 작성**:
   * `scripts/build_win64.ps1`, `scripts/build_win64.bat`, `scripts/build_linux.sh`.
10. **문서 및 라이선스 고지**:
    * `README.md`, `ARCHITECTURE.md`, `CREDITS.md`, `THIRD_PARTY_NOTICES.md`, `docs/guides/`, `docs/kb/` 갱신.
11. **GitHub Actions CI/Release 작성**:
    * `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

---

## 2. 검증 결과 (Verification Results)

### 2.1. Win64 Debug 빌드 및 CTest
```powershell
ctest --test-dir build/win64-debug -C Debug --output-on-failure
```
출력:
```
Test project E:/MYWORK/Projects/reexec/repsp/build/win64-debug
    Start 1: memory_probe
1/5 Test #1: memory_probe .....................   Passed    0.01 sec
    Start 2: module_probe
2/5 Test #2: module_probe .....................   Passed    0.01 sec
    Start 3: nid_probe
3/5 Test #3: nid_probe ........................   Passed    0.01 sec
    Start 4: recomp_contract_probe
4/5 Test #4: recomp_contract_probe ............   Passed    0.01 sec
    Start 5: platform_probe
5/5 Test #5: platform_probe ...................   Passed    0.34 sec

100% tests passed, 0 tests failed out of 5
Total Test time (real) = 0.40 sec
```

### 2.2. Win64 Release 빌드 및 CTest
```powershell
ctest --test-dir build/win64-debug -C Release --output-on-failure
```
출력:
```
Test project E:/MYWORK/Projects/reexec/repsp/build/win64-debug
    Start 1: memory_probe
1/5 Test #1: memory_probe .....................   Passed    0.02 sec
    Start 2: module_probe
2/5 Test #2: module_probe .....................   Passed    0.03 sec
    Start 3: nid_probe
3/5 Test #3: nid_probe ........................   Passed    0.02 sec
    Start 4: recomp_contract_probe
4/5 Test #4: recomp_contract_probe ............   Passed    0.02 sec
    Start 5: platform_probe
5/5 Test #5: platform_probe ...................   Passed    0.36 sec

100% tests passed, 0 tests failed out of 5
Total Test time (real) = 0.47 sec
```

### 2.3. 의존성 및 타이틀 격리 검증
* `git grep -i psprecomp`: 문서 설명 및 출처 표기 외 소스/헤더/빌드 파일에 일체 미출현 확인.
* `git grep -i djmax src/ include/ tools/`: 0건 (타이틀 이름 미출현 확인).
* `git grep -i dmp src/ include/ tools/`: 0건.

---

## 3. 기준안에서 바꾼 점과 이유 (Contract Deviations and Rationale)

1. **`GuardedRunResult` 멤버명 변경**:
   * 기준안: `struct GuardedRunResult { bool succeeded; uint32_t exception_code; ... };`
   * 변경: `uint32_t native_exception_code;`
   * 이유: Windows SDK `<excpt.h>`에 `#define exception_code _exception_code` 매크로가 정의되어 있어 MSVC 컴파일 시 충돌(`error C2039: '_exception_code': is not a member of 'repsp::platform::GuardedRunResult'`)이 발생함. 매크로 오염을 방지하기 위해 `native_exception_code`로 변경함.
2. **MSVC `_CRT_SECURE_NO_WARNINGS` 추가**:
   * `cmake/repsp_host_flags.cmake`에서 MSVC 컴파일 시 `_CRT_SECURE_NO_WARNINGS` 정의 추가.
   * 이유: `/WX` (경고를 오류로 처리) 환경에서 `getenv` 등 표준 C 라이브러리 함수 호출 시 `C4996` 경고가 발생하는 것을 방지함.

---

## 4. psprecomp에서 옮긴 코드와 커밋 (Carried Code)

* **없음**: psprecomp로부터 소스 코드를 복사하거나 옮겨오지 않았으며, 모든 모듈(`virtual_memory`, `guarded_run`, `host_thread`, `param_sfo`, `guest_memory`, `nid_registry`, `sha1`, `guest_cpu`)은 repsp의 사양과 C++20/C11 규칙에 맞추어 독자적으로 구현함.

---

## 5. 질문 (Questions for Reviewer)

1. **SDL 빌드 방식**:
   * 현재 단일 독립 실행 파일 생성과 바이너리 배포 편의성을 위해 SDL3 정적 라이브러리(`SDL_STATIC=ON`, `SDL_SHARED=OFF`)를 기본값으로 설정하고 `REPSP_SDL_SHARED` 옵션으로 공유 라이브러리 전환이 가능하도록 구성했습니다. 이 설정이 의도에 부합하는지 확인을 요청합니다.
2. **MIPS 스택 인자 오프셋**:
   * `repsp::recomp::Arg(n)`에서 8번째 이상의 인자를 `$sp + (n - 8) * 4`에서 읽도록 구현했습니다. MIPS 표준 호출 규약 및 실제 타이틀 컴파일러에 따라 프레임 오프셋 차이가 있을 수 있으므로 향후 reDMP1 lifter 어댑터 연동 시 세부 확인이 필요합니다.

---

# Work Log: Runtime Bootstrap (English)

See sections above for summary of work, verification outputs, contract adjustments, and questions for review.
