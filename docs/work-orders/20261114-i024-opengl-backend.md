# 작업 지시: OpenGL 렌더 백엔드 (issue #24)

설계: [20261114-i024](../design/20261114-i024-opengl-backend.md)
브랜치: `i024-opengl-backend`(main `e698cec`에서). 작업 위치는 reDMP1 안의 submodule `third_party/repsp`다.

1. platform: GL 문맥을 가진 창 모드(숨긴 창 포함), 렌더 작업 큐, GL 함수 주소 얻기, GL 대상 표시(설계 §2, §3).
2. 텍스처 풀기 코드를 두 백엔드가 공유하도록 정리(설계 §4). `ge_probe` 결과가 그대로여야 한다.
3. OpenGL 백엔드: 명령 버퍼, 렌더 대상, 셰이더, 텍스처 캐시, 묶기, 통계(설계 §3~§5).
4. display: GL 대상 표시와 CPU가 쓴 프레임버퍼 표시의 판정, 프레임 덤프(설계 §3).
5. probe `gl_probe`(설계 §6의 1). 합성 데이터만. 문맥이 없으면 건너뜀.
6. 문서: `docs/kb/ge.md`(래스터화 규칙의 대응, 블렌드의 근사, 프레임버퍼 알파; 확인 상태), `ARCHITECTURE.md`(타깃 표, 스레드 모델, platform 계약, 소비자 계약), 작업 로그 `docs/work-logs/20261114-i024-opengl-backend.md`.
7. 검증: 설계 §6.

지킬 것: 소프트웨어 백엔드가 기본값이다. `repsp_gpu`는 SDL 헤더를 포함하지 않는다. GL 호출은 UI 스레드에서만 한다. 새 외부 의존(로더 라이브러리, submodule)을 더하지 않는다. PPSSPP(GPL) 코드는 보지 않는다. 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다. 지원하지 않는 것은 통계와 한 번의 경고로 드러낸다. 게임 유래 데이터는 커밋하지 않는다. 문서와 로그에 사용자 계정 이름이 든 절대 경로를 쓰지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#24)`. push·PR·merge·tag 금지.

작업 환경에서 git 커밋이나 WSL 실행이 막히면 우회하지 말고, 변경을 작업 트리에 둔 채 작업 로그와 보고에 그 사실을 적는다. Linux에서도 빌드되도록 GCC 경고(`-Wall -Wextra -Wpedantic -Werror`)를 미리 살핀다.

완료 기준: 설계 §6의 1~3.

---

# Work order: OpenGL render backend (issue #24)

Design: [20261114-i024](../design/20261114-i024-opengl-backend.md)
Branch: `i024-opengl-backend` (from main `e698cec`), worked on in reDMP1's submodule `third_party/repsp`.

(1) Platform: a window mode with a GL context (including a hidden window), the render job queue, GL function addresses, presentation of a GL target (design §2, §3). (2) Rearrange the texture decoding code so both backends share it (design §4); `ge_probe` results must not change. (3) The OpenGL backend: command buffer, render targets, shaders, texture cache, batching, statistics (design §3-§5). (4) Display: deciding between presenting a GL target and a CPU-written frame buffer, and frame dumps (design §3). (5) The probe `gl_probe` (design §6, item 1), synthetic data only, skipped without a context. (6) Documents: `docs/kb/ge.md` (how the rasterization rules are matched, blend approximations, frame buffer alpha; confirmation status), `ARCHITECTURE.md` (target table, thread model, platform contract, consumer contract), the work log `docs/work-logs/20261114-i024-opengl-backend.md`. (7) Verify: design §6.

Must hold: the software backend is the default; `repsp_gpu` includes no SDL header; GL calls happen only on the UI thread; no new external dependency (loader library or submodule); PPSSPP (GPL) code is not read; no title name or address in code, probes or documents; anything unsupported shows in statistics and a one-time warning; no game-derived data is committed; no absolute path containing a user account name in documents or logs. Commit per unit of work, titles ending in `(#24)`; no push, pull request, merge or tag.

If git commits or WSL are blocked in the working environment, do not work around it: leave the changes in the working tree and say so in the work log and the report. Watch for GCC warnings (`-Wall -Wextra -Wpedantic -Werror`) so the Linux build passes.

Done when: design §6, items 1-3.
