# 작업 지시: 안전점에서 시간이 지난 대기 깨우기 (issue #69)

설계: [20261121-i069](../design/20261121-i069-poll-timeouts.md)
브랜치: `i69-poll-timeouts`(main `0249592`에서). 작업 위치는 reDMP2 안의 submodule `third_party/repsp`다.

1. `src/runtime/scheduler.cpp`: `ExpireTimedWaitsLocked(now)` 추출, 유휴 경로와 `Poll()`에서 사용(설계 §2). 시간 제한 대기 스레드 수(대기 시작·종료·삭제·취소 경로에서 정확히 유지)로 시계 읽기를 건너뛴다. 공개 헤더 변경이 필요하면 최소로.
2. probe(설계 §3의 1), CTest `TIMEOUT`.
3. 문서: `ARCHITECTURE.md`(스케줄러의 전환 시점), 작업 로그 `docs/work-logs/20261121-i069-poll-timeouts.md`.
4. 검증: 설계 §3. 소비자 측정은 reDMP2 Release를 이 submodule 작업 트리로 다시 빌드해 `redmp2.exe roms --headless --max-vblanks 600 --save-dir <저장소 밖> --crash-dir <저장소 밖>`로 한다. 표시 프레임 수, `sceKernelSetEventFlag` 호출 수, 스레드 상태, 정지 분류를 보고한다. 새로 미구현 NID에서 멈추면 그것도 보고한다(이 issue에서 구현하지 않는다).

지킬 것: 우선순위·FIFO·선점 규칙을 바꾸지 않는다. 바꿔야 할 것 같으면 멈추고 묻는다. 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다. PPSSPP 코드는 보지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#69)`. push·PR·merge·tag 금지. reDMP2 파일은 바꾸지 않는다.

완료 기준: 설계 §3의 1~4.

---

# Work order: waking expired timed waits at safe points (issue #69)

Design: [20261121-i069](../design/20261121-i069-poll-timeouts.md)
Branch: `i69-poll-timeouts` (from main `0249592`), worked on in reDMP2's submodule `third_party/repsp`.

(1) `src/runtime/scheduler.cpp`: extract `ExpireTimedWaitsLocked(now)` and use it in the idle path and in `Poll()` (design §2); skip the clock read using a count of timed waiters kept exact on every wait start, end, deletion and cancellation path; keep public header changes minimal. (2) The probe (design §3 item 1) with a CTest `TIMEOUT`. (3) Documents: `ARCHITECTURE.md` (the scheduler's switch points), the work log `docs/work-logs/20261121-i069-poll-timeouts.md`. (4) Verify: design §3; the consumer measurement rebuilds reDMP2 Release with this submodule working tree and runs `redmp2.exe roms --headless --max-vblanks 600 --save-dir <outside the repo> --crash-dir <outside the repo>`; report frames presented, the `sceKernelSetEventFlag` call count, thread states and the stop class; if it now stops on a new unimplemented NID, report that too (not implemented in this issue).

Must hold: priorities, FIFO and the preemption rules do not change (stop and ask if they seem to need to); no title name or address in code, probes or documents; PPSSPP code is not read; commit per unit of work with titles ending in `(#69)`; no push, pull request, merge or tag; no reDMP2 file changes.

Done when: design §3 items 1 to 4.
