# 작업 지시: 런타임 부트스트랩 (issue #1)

설계: [20261111-i001](../design/20261111-i001-runtime-bootstrap.md)

이미 된 것(main `f953910`, 작업 브랜치 `i001-runtime-bootstrap`): `LICENSE`, `.gitignore`, `.gitattributes`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `docs/CODING_STYLE.md`, `docs/PROJECT_CHARTER.md`, 이 설계와 작업 지시, `docs/kb/README.md`.

1. 빌드 골격: 루트 `CMakeLists.txt`(C11/C++20, 레이어 타깃과 별칭 `repsp::*`, `REPSP_BUILD_PROBES`), `CMakePresets.json`(`win64-debug`, `win64-release`, `linux-x64-debug`, `linux-x64-release`), `cmake/repsp_host_flags.cmake`(`repsp_apply_host_flags`, 경고 수준), `VERSION`(`0.0.0`). reDMP1이 바로 `add_subdirectory`할 수 있도록 이 단계를 먼저 커밋한다.
2. `third_party/SDL` submodule(https://github.com/libsdl-org/SDL, `release-3.4.x`의 커밋 또는 태그), SDL을 정적 또는 공유로 빌드하는 설정(어느 쪽인지와 근거를 작업 로그에).
3. `repsp_platform`: 설계 §3의 표(주소 공간, 보호 호출, 스레드, 경로, SDL3 초기화·계약 선언). Win32 구현과 Linux 구현.
4. `repsp_module`: `PARAM.SFO` 파서(공개 헤더 `include/repsp/module/param_sfo.h`).
5. `repsp_runtime`: `GuestMemory`(설계 §4), NID 레지스트리와 HLE 호출 진입점(설계 §6), SHA-1, 로그.
6. `repsp_recomp`: `include/repsp/recomp/guest_cpu.h`(설계 §5의 계약), `Arg`/`Return` 도우미, C용 메모리 접근 함수.
7. `src/hle/`: 등록 진입점의 자리(`repsp_hle_kernel` 하나, 함수 없음)와 `README.md`(모듈 경계 원칙).
8. probe 다섯 개(설계 §9-2)를 `tools/*_probe/`에 두고 CTest에 등록. 테스트 데이터는 모두 손으로 만든 합성 데이터.
9. 스크립트: `scripts/build_win64.ps1`/`.bat`, `scripts/build_linux.sh`(configure + build + ctest).
10. 문서: `README.md`(목적, 소비자 사용법, 빌드, 상태, 라이선스), `ARCHITECTURE.md`(구현된 타깃 표와 소비자 계약 절: CMake 사용법, `guest_cpu.h` 콜백 표, 메모리 C 함수, NID 호출 규칙), `docs/kb/`(PSP 메모리 맵, `PARAM.SFO` 형식, NID — 출처 링크와 확인 상태 표기, 색인 갱신), `docs/guides/{README,win64-setup-and-build}.md`, `THIRD_PARTY_NOTICES.md`(SDL3 zlib; psprecomp 코드를 옮기면 그 항목), `CREDITS.md`.
11. CI: `.github/workflows/ci.yml`(Win64 Debug/Release on `windows-2022`, Linux x64 Debug/Release on `ubuntu-24.04`, submodules recursive, 빌드 + CTest), `release.yml`(태그 = `VERSION`, `main` 위, 릴리스 노트 본문).
12. 작업 로그: `docs/work-logs/20261111-i001-runtime-bootstrap.md`(한 일, 검증 결과, 계약을 기준안에서 바꾼 점과 이유, psprecomp에서 옮긴 코드와 그 커밋, 질문).

지켜야 할 것: psprecomp의 헤더·소스를 include하거나 submodule로 두지 않는다(옮긴 코드는 repsp 타입으로 고친 사본이며 `THIRD_PARTY_NOTICES.md`에 적는다). 타이틀 이름·주소를 코드에 쓰지 않는다. 커밋은 작업 단위마다, 제목 끝에 `(#1)`.

완료 기준: 설계 §9의 1~4.

---

# Work order: runtime bootstrap (issue #1)

Design: [20261111-i001](../design/20261111-i001-runtime-bootstrap.md)

Already done (main `f953910`, task branch `i001-runtime-bootstrap`): `LICENSE`, `.gitignore`, `.gitattributes`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `docs/CODING_STYLE.md`, `docs/PROJECT_CHARTER.md`, this design and work order, `docs/kb/README.md`.

(1) Build skeleton: the root `CMakeLists.txt` (C11/C++20, layer targets with `repsp::*` aliases, `REPSP_BUILD_PROBES`), `CMakePresets.json` (`win64-debug`, `win64-release`, `linux-x64-debug`, `linux-x64-release`), `cmake/repsp_host_flags.cmake` (`repsp_apply_host_flags`, warning levels), `VERSION` (`0.0.0`); commit this step first so reDMP1 can `add_subdirectory` it right away. (2) The `third_party/SDL` submodule (https://github.com/libsdl-org/SDL, a `release-3.4.x` commit or tag) and the setting that builds SDL static or shared (which, and why, in the work log). (3) `repsp_platform`: the design §3 table (address space, guarded calls, threads, paths, SDL3 initialization and contract declarations), Win32 and Linux implementations. (4) `repsp_module`: the `PARAM.SFO` parser (public header `include/repsp/module/param_sfo.h`). (5) `repsp_runtime`: `GuestMemory` (design §4), the NID registry and HLE call entry (design §6), SHA-1, logging. (6) `repsp_recomp`: `include/repsp/recomp/guest_cpu.h` (the design §5 contract), the `Arg`/`Return` helpers, the C memory-access functions. (7) `src/hle/`: the registration entry slot (a single `repsp_hle_kernel` with no functions) and a `README.md` (module boundary principles). (8) The five probes (design §9-2) under `tools/*_probe/`, registered in CTest, with hand-made synthetic test data only. (9) Scripts: `scripts/build_win64.ps1`/`.bat` and `scripts/build_linux.sh` (configure + build + ctest). (10) Documents: `README.md` (purpose, consumer usage, build, status, license), `ARCHITECTURE.md` (the implemented target table and a consumer-contract section: CMake usage, the `guest_cpu.h` callback table, the C memory functions, the NID call rules), `docs/kb/` (the PSP memory map, the `PARAM.SFO` format, NIDs, with source links and confirmation status, index updated), `docs/guides/{README,win64-setup-and-build}.md`, `THIRD_PARTY_NOTICES.md` (SDL3 zlib; an entry for any carried psprecomp code), `CREDITS.md`. (11) CI: `.github/workflows/ci.yml` (Win64 Debug/Release on `windows-2022`, Linux x64 Debug/Release on `ubuntu-24.04`, recursive submodules, build + CTest) and `release.yml` (tag = `VERSION`, on `main`, release note as the body). (12) Work log: `docs/work-logs/20261111-i001-runtime-bootstrap.md` (what was done, verification results, contract changes from the baseline and why, code carried from psprecomp with its commit, questions).

Must hold: no psprecomp header or source is included or kept as a submodule (carried code is a copy reshaped to repsp types and listed in `THIRD_PARTY_NOTICES.md`); no title name or address appears in code. Commit per unit of work, titles ending in `(#1)`.

Done when: design §9, items 1-4.

---

## 리뷰 1 (2026-11-12, `4cf4d72` 기준)

검증: Win64 Debug/Release 빌드와 CTest 5/5, Linux x64(WSL Ubuntu 24.04, GCC 13) 빌드와 CTest 5/5를 리뷰어가 다시 실행해 통과를 확인했다. 아래를 고친다.

| # | 위치 | 문제 | 요청 |
|---|---|---|---|
| R1 | `CMakePresets.json` | Win64 preset에 generator가 없어 `architecture`(strategy `external`)가 무시되고 x64가 보장되지 않는다. | `"generator": "Visual Studio 17 2022"`, `architecture` `{ "value": "x64", "strategy": "set" }`. |
| R2 | `src/platform/win32/guarded_run_win32.cpp` | 필터가 모든 SEH 예외를 `EXECUTE_HANDLER`로 삼킨다. C++ 예외(`0xE06D7363`)와 브레이크포인트도 정지 보고가 된다. | 하드웨어 예외(접근 위반, in-page error, illegal/privileged instruction, 정수 0 나누기·overflow, stack overflow, datatype misalignment)만 잡고 나머지는 `EXCEPTION_CONTINUE_SEARCH`. stack overflow를 잡으면 `_resetstkoflw()`. |
| R3 | `src/platform/linux/guarded_run_linux.cpp` | 호출마다 `sigaction`을 설치·복원한다. 처리기는 프로세스 전체 상태라 두 스레드의 동시 호출이 서로를 덮어쓰고, 중첩 호출은 jump buffer를 덮어쓴다. SIGSEGV·SIGBUS만 잡는다. | 처리기는 한 번만 설치(`std::call_once`)하고 보호 구간 밖의 시그널은 이전 처리기로 넘긴다. 스레드별 깊이와 jump buffer 스택. SIGILL, SIGFPE 추가. 잡힌 fault는 재개하지 않고 정지 보고에만 쓴다는 점을 주석에 적는다(`siglongjmp`는 C++ 소멸자를 건너뛴다). platform_probe에 두 스레드 동시 보호 호출과 중첩 보호 호출 검사를 더한다. |
| R4 | `GuestMemory` | 창이 160 MiB(`0x0A000000`)인데 `Translate`는 `& 0x1FFFFFFF`로 512 MiB까지 낸다. `phys >= 0x0A000000`(잘못된 포인터, I/O 레지스터 `0x1C000000`대)이면 예약 밖 호스트 메모리를 가리킨다. 원인은 설계 기준안이며 설계를 고쳤다(`5140896`). | `kWindowSize = 0x20000000`. memory_probe에 `0x1FFFFFFC` 같은 영역 밖 주소의 읽기가 보호 호출로 잡히는 검사를 더한다. kb `psp-memory-map.md`도 맞춘다. |
| R5 | `include/repsp/recomp/recomp_helpers.h` | `Arg<T*>`가 임의 포인터 형으로 게스트 메모리를 호스트 포인터로 바꿔 준다. 규칙상 게스트 구조체를 호스트 포인터로 다루지 않는다. | 포인터 형은 `uint8_t*`, `const uint8_t*`, `char*`, `const char*`만 허용(`static_assert`). 구조체는 `GuestAddress`로 받아 `GuestMemory` 접근 함수로 읽는다. |
| R6 | `docs/kb/*.md`, `docs/kb/README.md` | 세 문서가 **확인됨**인데 근거가 외부 자료뿐이다. 규칙상 우리가 다시 확인하기 전까지 **추정**이다. | 메모리 맵은 **추정**(출처 유지). `PARAM.SFO`는 형식 설명은 **추정**, 합성 데이터 probe로 확인한 파서 동작만 따로 적는다(실제 디스크로 확인은 reDMP1 issue #1에서). NID는 R7 후 공개 NID와 맞으면 **확인됨**. 문서 안에서 항목별로 표시한다. |
| R7 | `tools/nid_probe` | 자기 SHA-1로 계산한 값과 자기 `ComputeNid`를 비교할 뿐이라 SHA-1이 틀려도 통과한다. | SHA-1 표준 벡터(FIPS 180: `""`, `"abc"`, 448비트 메시지, 백만 개의 `a`)와 공개 NID 3개 이상(예: `sceKernelExitGame`, `sceDisplaySetMode`, `sceCtrlReadBufferPositive`; 값은 psdevwiki 또는 uOFW에서 확인하고 출처를 주석에 적는다)으로 검사한다. psprecomp의 코드는 가져오지 않는다. |

작업 로그의 질문에 대한 답:

* SDL 정적 빌드를 기본으로 하고 `REPSP_SDL_SHARED`로 바꾸는 구성은 그대로 둔다.
* 9번째 이후 인자의 `$sp + (n - 8) * 4`는 **추정**으로 두고 `recomp_helpers.h` 주석과 kb에 그렇게 적는다. 인자가 8개를 넘는 펌웨어 함수를 처음 구현하는 task에서 확인한다.

완료 기준: R1~R7 반영, Win64 Debug/Release와 Linux x64 빌드·CTest 통과, 작업 로그에 "리뷰 1 반영" 절 추가.

## Review 1 (2026-11-12, at `4cf4d72`)

Verification: the reviewer re-ran the Win64 Debug/Release builds with CTest 5/5 and the Linux x64 (WSL Ubuntu 24.04, GCC 13) build with CTest 5/5; all pass. Fix the items in the table above: R1 the Win64 presets need the Visual Studio 17 2022 generator with `architecture` strategy `set`; R2 the Win32 filter must catch hardware exceptions only and pass C++ exceptions and breakpoints on (`EXCEPTION_CONTINUE_SEARCH`), calling `_resetstkoflw()` after a stack overflow; R3 the Linux handler is installed once, chains signals outside guarded regions to the previous handler, keeps a per-thread depth and jump-buffer stack, adds SIGILL and SIGFPE, documents that a caught fault is never resumed, and platform_probe gains concurrent and nested guarded-call checks; R4 `kWindowSize` becomes `0x20000000` so every folded address stays inside the reservation (the design baseline's 160 MiB was the cause and is fixed in `5140896`), with a memory_probe check that an out-of-region read such as `0x1FFFFFFC` is caught and the kb memory map updated; R5 `Arg<T*>` accepts only byte and char pointers (`static_assert`), structures being read through `GuestMemory` by `GuestAddress`; R6 the kb documents mark each statement, external-only facts as **inferred**, the synthetic-probe-checked parser behavior separately, and NIDs **confirmed** only after R7; R7 `nid_probe` checks the FIPS 180 SHA-1 vectors and at least three published NIDs with their sources cited in comments, taking no code from psprecomp.

Answers to the work log's questions: keep SDL static by default with `REPSP_SDL_SHARED` to switch; keep `$sp + (n - 8) * 4` for the ninth argument on as **inferred**, say so in the `recomp_helpers.h` comment and the kb, and confirm it in the first task that implements a firmware function with more than eight arguments.

Done when: R1-R7 are in, the Win64 Debug/Release and Linux x64 builds and CTest pass, and the work log has a "Review 1" section.
