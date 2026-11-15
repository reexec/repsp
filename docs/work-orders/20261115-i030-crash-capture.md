# 작업 지시: 크래시 기록 (issue #30, 함께 #31)

설계: [20261115-i030](../design/20261115-i030-crash-capture.md)
브랜치: `i030-crash-capture`(main `e3beb45`에서). 작업 위치는 reDMP1 안의 submodule `third_party/repsp`다.

1. 크래시 처리기: 네이티브 예외, `abort`/`std::terminate`, Linux 신호. 미니덤프와 크래시 보고(설계 §2). 보고에는 **정지 이유**와 **그 시점에 스케줄러 뮤텍스를 가진 스레드**도 넣는다(소비자의 첫 크래시가 스케줄러 뮤텍스를 쥔 채 `GuardedRun`을 빠져나온 것이었다; 소비자 설계 §4).
2. 게스트 최근 실행 기록(HLE 호출, 게스트 함수 진입)의 고리 버퍼와 소비자 계약(설계 §2, §4).
3. 실행 로그 파일, 입력 기록(설계 §3).
4. 뮤텍스를 쥔 채 정지나 네이티브 오류가 일어날 수 있는 경로의 조사와 재현 probe. 고치는 방향은 설계 판단이므로 근거와 선택지를 질문으로 올린다(소비자 설계 §4).
5. issue #31: 모든 probe에 CTest `TIMEOUT`을 준다. `audio_probe`의 간헐적 멈춤을 반복 실행으로 재현해 원인을 찾는다(리뷰어가 로컬 Win64 Release에서 300회 반복했을 때는 재현되지 않았다; CI의 느린 머신에서 멈췄다). 원인이 확정되면 고치고, 아니면 측정과 가설을 기록한다.
6. probe `crash_probe`와 입력 기록 probe(설계 §5).
7. 문서: `docs/kb/`(크래시 처리, 정지와 되감기의 규칙, 입력 기록 형식), `ARCHITECTURE.md`(플랫폼 계약, 소비자 계약), 작업 로그 `docs/work-logs/20261115-i030-crash-capture.md`.
8. 검증: 설계 §5.

지킬 것: 정상 실행의 비용을 무시할 만하게 유지한다. 처리기 안에서 할당과 잠금을 피한다. 스케줄러의 의미를 바꾸지 않는다(정지와 되감기 규칙의 변경은 질문으로). 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다. PPSSPP(GPL) 코드는 보지 않는다. 덤프와 게임 유래 데이터는 커밋하지 않는다. 문서와 로그에 사용자 계정 이름이 든 절대 경로를 쓰지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#30)` 또는 `(#31)`. push·PR·merge·tag 금지.

작업 환경에서 git 커밋이나 WSL 실행이 막히면 우회하지 말고, 변경을 작업 트리에 둔 채 작업 로그와 보고에 그 사실을 적는다. Linux에서도 빌드되도록 GCC 경고(`-Wall -Wextra -Wpedantic -Werror`)를 미리 살핀다.

완료 기준: 설계 §5의 1~4, 그리고 5번 항목(타임아웃과 멈춤 조사).

---

# Work order: crash capture (issue #30, with #31)

Design: [20261115-i030](../design/20261115-i030-crash-capture.md)
Branch: `i030-crash-capture` (from main `e3beb45`), worked on in reDMP1's submodule `third_party/repsp`.

(1) The crash handler: native exceptions, `abort`/`std::terminate`, Linux signals; minidump and crash report (design §2). The report also carries **the stop reason** and **the thread holding the scheduler mutex at that moment** (the consumer's first crash was a guest thread leaving `GuardedRun` while holding the scheduler mutex; consumer design §4). (2) Ring buffers of recent guest execution (HLE calls, guest function entries) and their consumer contract (design §2, §4). (3) The run log file and input recording (design §3). (4) Investigation of the paths where a stop or native fault can happen with a mutex held, and a reproducing probe; how to fix it is a design decision, so evidence and options are raised as a question (consumer design §4). (5) Issue #31: give every probe a CTest `TIMEOUT`; reproduce the intermittent `audio_probe` hang by running it repeatedly (the reviewer's 300 local Win64 Release runs did not reproduce it; it hung on CI's slower machine); fix it if the cause is confirmed, otherwise record measurements and hypotheses. (6) The probes `crash_probe` and the input recording probe (design §5). (7) Documents: `docs/kb/` (crash handling, the rules of stops and unwinding, the input recording format), `ARCHITECTURE.md` (platform and consumer contracts), the work log `docs/work-logs/20261115-i030-crash-capture.md`. (8) Verify: design §5.

Must hold: cost in normal runs stays negligible; no allocation or locks inside the handler; scheduler semantics do not change (changes to the rules of stops and unwinding are raised as questions); no title name or address in code, probes or documents; PPSSPP (GPL) code is not read; dumps and game-derived data are never committed; no absolute path containing a user account name in documents or logs. Commit per unit of work, titles ending in `(#30)` or `(#31)`; no push, pull request, merge or tag.

If git commits or WSL are blocked in the working environment, do not work around it: leave the changes in the working tree and say so in the work log and the report. Watch for GCC warnings (`-Wall -Wextra -Wpedantic -Werror`) so the Linux build passes.

Done when: design §5, items 1-4, plus item 5 above (timeouts and the hang investigation).
