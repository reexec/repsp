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
