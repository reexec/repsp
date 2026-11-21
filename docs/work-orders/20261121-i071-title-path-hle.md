# 작업 지시: 오프닝 영상에서 타이틀 화면까지의 부팅 경로 HLE (issue #71)

설계: [20261121-i071](../design/20261121-i071-title-path-hle.md)
브랜치: `i71-title-path-hle`(main `a730166`에서). 작업 위치는 reDMP2 안의 submodule `third_party/repsp`다.

1. 측정 반복(설계 §2): reDMP2 Release를 이 submodule 작업 트리로 다시 빌드하고 다음처럼 실행한다. 멈춘 NID를 구현하고 다시 실행한다.
   ```
   redmp2.exe roms --headless --max-vblanks <N> --input-script <스크립트> \
     --codec-plugin E:\MYWORK\Projects\reexec\reDMP1\build\win64-release-ffmpeg\Release\repsp_codec_ffmpeg.dll \
     --save-dir <저장소 밖> --crash-dir <저장소 밖>
   ```
   FFmpeg DLL을 찾도록 실행할 때 `PATH` 앞에 그 플러그인 디렉터리를 둔다. 그 디렉터리의 파일은 읽기만 하고 복사·커밋하지 않는다. 화면 확인은 `REPSP_FRAME_DUMP=<저장소 밖 디렉터리>`(BMP)로 한다.
2. 각 구현의 probe, `ARCHITECTURE.md`(필요하면), 작업 로그 `docs/work-logs/20261121-i071-title-path-hle.md`.
3. 검증: 설계 §3.

지킬 것: 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다(작업 로그의 소비자 측정 기록은 소비자 issue 링크와 주소 없는 설명으로). PPSSPP 코드는 보지 않는다. 반환값과 오류 코드는 출처가 있는 것만. 게임 유래 데이터(프레임 덤프 포함)는 커밋하지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#71)`. push·PR·merge·tag 금지. reDMP2 파일은 바꾸지 않는다.

완료 기준: 설계 §2의 멈춤 기준, §3의 1~3.

---

# Work order: boot-path HLE from the opening movie to the title screen (issue #71)

Design: [20261121-i071](../design/20261121-i071-title-path-hle.md)
Branch: `i71-title-path-hle` (from main `a730166`), worked on in reDMP2's submodule `third_party/repsp`.

(1) The measured loop (design §2): rebuild reDMP2 Release with this submodule working tree and run it as shown above; implement the NID it stops on and run again. Put that plugin directory first in `PATH` for the run so the FFmpeg DLLs are found; its files are only read, never copied or committed. Check the screen with `REPSP_FRAME_DUMP=<directory outside the repo>` (BMP). (2) A probe per implementation, `ARCHITECTURE.md` where needed, the work log `docs/work-logs/20261121-i071-title-path-hle.md`. (3) Verify: design §3.

Must hold: no title name or address in code, probes or documents (consumer-side records in the work log use the consumer issue link and descriptions without addresses); PPSSPP code is not read; only sourced return values and error codes; no game-derived data (frame dumps included) committed; commit per unit of work with titles ending in `(#71)`; no push, pull request, merge or tag; no reDMP2 file changes.

Done when: the design §2 stopping rule, and design §3 items 1 to 3.
