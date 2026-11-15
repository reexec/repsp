# 작업 지시: 소리 결함 (issue #26)

설계: [20261115-i026](../design/20261115-i026-sound-defects.md)
브랜치: `i026-sound-defects`(main `ad7e9dd`에서). 작업 위치는 reDMP1 안의 submodule `third_party/repsp`다.

1. 진단 기능: 채널별·SAS 출력 WAV, SAS 음성 추적, ATRAC 추적(설계 §3).
2. 소비자의 조사에서 확정된 원인의 수정과 probe(설계 §2, §4).
3. 문서: `docs/kb/`의 SAS·ATRAC·sceAudio 항목(확인 상태와 출처), 진단 기능의 설명, 작업 로그 `docs/work-logs/20261115-i026-sound-defects.md`(결함마다 증상, 증거, 원인, 수정, probe).
4. 검증: 설계 §5.

지킬 것: 추측으로 고치지 않는다. 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다. PPSSPP(GPL) 코드와 FFmpeg의 구현 소스는 보지 않는다(FFmpeg 명령줄 도구를 동작 비교에 쓰는 것은 된다). 스케줄러의 의미와 소비자 계약은 바꾸지 말고 질문으로 올린다. 모든 수정에 probe. 게임 유래 데이터(추출한 소리 포함)는 커밋하지 않는다. 문서와 로그에 사용자 계정 이름이 든 절대 경로를 쓰지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#26)`. push·PR·merge·tag 금지.

작업 환경에서 git 커밋이나 WSL 실행이 막히면 우회하지 말고, 변경을 작업 트리에 둔 채 작업 로그와 보고에 그 사실을 적는다. Linux에서도 빌드되도록 GCC 경고(`-Wall -Wextra -Wpedantic -Werror`)를 미리 살핀다.

완료 기준: 설계 §5의 1~3.

---

# Work order: sound defects (issue #26)

Design: [20261115-i026](../design/20261115-i026-sound-defects.md)
Branch: `i026-sound-defects` (from main `ad7e9dd`), worked on in reDMP1's submodule `third_party/repsp`.

(1) Diagnostics: per-channel and SAS output WAVs, the SAS voice trace, the ATRAC trace (design §3). (2) Fixes, with probes, for the causes confirmed by the consumer's investigation (design §2, §4). (3) Documents: the SAS, ATRAC and sceAudio entries in `docs/kb/` (confirmation status and sources), a description of the diagnostics, the work log `docs/work-logs/20261115-i026-sound-defects.md` (symptom, evidence, cause, fix and probe per defect). (4) Verify: design §5.

Must hold: no fixing by guesswork; no title name or address in code, probes or documents; PPSSPP (GPL) code and FFmpeg's implementation source are not read (using the FFmpeg command-line tool for behaviour comparison is fine); scheduler semantics and consumer contracts are not changed but raised as questions; a probe for every fix; no game-derived data (extracted sounds included) is committed; no absolute path containing a user account name in documents or logs. Commit per unit of work, titles ending in `(#26)`; no push, pull request, merge or tag.

If git commits or WSL are blocked in the working environment, do not work around it: leave the changes in the working tree and say so in the work log and the report. Watch for GCC warnings (`-Wall -Wextra -Wpedantic -Werror`) so the Linux build passes.

Done when: design §5, items 1-3.
