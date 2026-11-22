# 작업 지시: 타이틀 화면에서 한 곡 플레이까지의 HLE (issue #73)

설계: [20261121-i073](../design/20261121-i073-song-path-hle.md)
브랜치: `i73-song-path-hle`(main `a243bed`에서). 작업 위치는 reDMP2 안의 submodule `third_party/repsp`다.

1. 측정 반복(설계 §2): 이 submodule 작업 트리로 reDMP2의 FFmpeg 플러그인 빌드(`build\win64-release-ffmpeg`, `REDMP2_CODEC_FFMPEG=ON`, 플러그인이 실행 파일 옆에 놓여 자동으로 읽힘)를 다시 빌드하고 아래처럼 실행한다. 멈춘 NID를 구현하고 다시 실행한다.
   ```
   build\win64-release-ffmpeg\Release\redmp2.exe roms --headless --max-vblanks <N> --input-script <스크립트> \
     --save-dir <저장소 밖> --crash-dir <저장소 밖>
   ```
   * 입력 스크립트는 reDMP2 `docs/work-logs/20261121-i009-title-screen.md` §3의 타이틀까지 입력으로 시작한다. 그 뒤에 `1800 START`, 모드 선택, 곡 선택, 플레이 입력을 더한다.
   * 화면 확인은 `REPSP_FRAME_DUMP=<저장소 밖 디렉터리>`(BMP)로 한다.
   * 저장 디렉터리는 반복마다 비우거나 새로 만들어 첫 실행 경로를 재현한다.
2. 각 구현의 probe, `ARCHITECTURE.md`(필요하면), 작업 로그 `docs/work-logs/20261121-i073-song-path-hle.md`.
3. 검증: 설계 §3.

지킬 것:
* 타이틀 이름과 주소를 코드·probe·문서에 쓰지 않는다. 작업 로그의 소비자 측정 기록에는 소비자 issue 링크와 주소 없는 설명만 쓴다.
* PPSSPP 코드는 보지 않는다. 반환값과 오류 코드는 출처가 있는 것만 쓴다.
* 게임 유래 데이터(프레임 덤프 포함)는 커밋하지 않는다.
* 작업 단위마다 커밋하고, 제목 끝에 `(#73)`을 붙인다.
* push·PR·merge·tag 금지. reDMP2 파일은 바꾸지 않는다.

디스패치 미스가 나오면 근거(주소, 그 주소를 만드는 코드)와 함께 멈추고 보고한다.

완료 기준: 설계 §2의 멈춤 기준, §3의 1~3.

---

# Work order: HLE from the title screen to one played song (issue #73)

Design: [20261121-i073](../design/20261121-i073-song-path-hle.md)
Branch: `i73-song-path-hle` (from main `a243bed`), worked on in reDMP2's submodule `third_party/repsp`.

1. The measured loop (design §2): rebuild reDMP2's FFmpeg plugin build with this submodule working tree, then run it as shown above. Implement the NID it stops on and run again.
   * The build is `build\win64-release-ffmpeg` with `REDMP2_CODEC_FFMPEG=ON`; the plugin sits next to the executable and loads automatically.
   * The input script starts with the inputs up to the title from reDMP2 `docs/work-logs/20261121-i009-title-screen.md` §3. Append `1800 START`, then the mode select, song select and play inputs.
   * Check the screen with `REPSP_FRAME_DUMP=<directory outside the repo>` (BMP).
   * Empty or recreate the save directory each iteration so the first-run path is reproduced.
2. A probe per implementation, `ARCHITECTURE.md` where needed, and the work log `docs/work-logs/20261121-i073-song-path-hle.md`.
3. Verify: design §3.

Must hold:
* No title name or address in code, probes or documents. Consumer-side records in the work log use the consumer issue link and descriptions without addresses.
* PPSSPP code is not read. Only sourced return values and error codes.
* No game-derived data (frame dumps included) committed.
* Commit per unit of work, with titles ending in `(#73)`.
* No push, pull request, merge or tag; no reDMP2 file changes.

On a dispatch miss, stop and report with evidence: the address and the code that builds it.

Done when: the design §2 stopping rule, and design §3 items 1 to 3.
