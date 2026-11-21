# 작업 지시: 부팅 경로 HLE와 "사용 불가" 애드혹 네트워크 (issue #67)

설계: [20261121-i067](../design/20261121-i067-boot-path-hle.md)
브랜치: `i67-boot-path-hle`(main `81558ef`에서). 작업 위치는 reDMP2 안의 submodule `third_party/repsp`다.

1. `tools/nid_hash.py`(표준 라이브러리만): 이름 → NID 계산, NID 목록과 후보 이름 목록의 대조. 미등록 NID의 이름을 확인해 `src/runtime/nid_names.cpp`에 더한다. 확인 못 한 NID는 작업 로그에.
2. 부팅 경로 반복(설계 §2의 2): 소비자 `redmp2`를 헤드리스로 실행해 멈춘 NID를 구현하고 다시 실행한다. 실행에는 반드시 `--save-dir`와 `--crash-dir`를 저장소 밖 임시 디렉터리로 준다. 설계 §1의 멈춤 기준에 닿으면 끝낸다. 각 반복을 작업 로그에 적는다.
3. `repsp_hle_net`(설계 §2의 3). 소비자에 연결하는 것(`Register()` 호출)은 이 저장소의 일이 아니다. 소비자에서 시험할 때는 소비자 작업 트리를 임시로 바꾸지 말고, 리드에게 보고한다. 단, 2단계 진행을 위해 네트워크 NID가 불려 멈추면 그 사실을 보고하고 멈춘다.
4. 작은 함수(설계 §2의 4)는 2단계에서 불리면.
5. probe와 문서: 각 구현의 probe, `ARCHITECTURE.md`(새 모듈), `docs/kb/`(필요하면), 작업 로그 `docs/work-logs/20261121-i067-boot-path-hle.md`.
6. 검증: 설계 §3.

지킬 것: 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다. PPSSPP 코드는 보지 않는다. 반환값과 오류 코드는 출처가 있는 것만. 게임 유래 데이터는 커밋하지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#67)`. push·PR·merge·tag 금지. reDMP2 파일은 바꾸지 않는다.

완료 기준: 설계 §1의 멈춤 기준에 닿고, §3의 1~4.

---

# Work order: boot-path HLE and an "unavailable" ad-hoc network (issue #67)

Design: [20261121-i067](../design/20261121-i067-boot-path-hle.md)
Branch: `i67-boot-path-hle` (from main `81558ef`), worked on in reDMP2's submodule `third_party/repsp`.

(1) `tools/nid_hash.py` (standard library only): name → NID, and matching a NID list against candidate names; confirm the names of unregistered NIDs and add them to `src/runtime/nid_names.cpp`; unconfirmed NIDs go into the work log. (2) The boot-path loop (design §2 step 2): run the consumer `redmp2` headless, implement the NID it stops on, run again; always pass `--save-dir` and `--crash-dir` pointing at a temporary directory outside the repository; end at the design §1 stopping rule; record each iteration in the work log. (3) `repsp_hle_net` (design §2 step 3); wiring it into the consumer (the `Register()` call) is not this repository's job, so do not change the consumer's working tree to try it; report to the lead instead, and if the loop stops on a network NID, report that and stop. (4) Small functions (design §2 step 4) when step 2 reaches them. (5) Probes and documents: a probe per implementation, `ARCHITECTURE.md` (the new module), `docs/kb/` where needed, the work log `docs/work-logs/20261121-i067-boot-path-hle.md`. (6) Verify: design §3.

Must hold: no title name or address in code, probes or documents; PPSSPP code is not read; only sourced return values and error codes; no game-derived data committed; commit per unit of work with titles ending in `(#67)`; no push, pull request, merge or tag; no reDMP2 file changes.

Done when: the design §1 stopping rule is reached, and design §3 items 1 to 4.
