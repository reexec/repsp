# 작업 지시: 코덱 플러그인 (issue #21)

설계: [20261114-i021](../design/20261114-i021-codec-plugin.md)
브랜치: `i021-codec-plugin`(main `7c80806`에서). 작업 위치는 reDMP1 안의 submodule `third_party/repsp`다.

1. 플러그인 계약과 로더(설계 §2). 필요하면 platform에 동적 라이브러리 열기 기능.
2. 디코더 계약 변경: 스트림별 문맥, 넓힌 `AtracFrameInfo`(설계 §3). `repsp_hle_atrac`·`repsp_hle_mpeg`를 맞춘다.
3. 플러그인 `repsp_codec_ffmpeg`: ATRAC3·ATRAC3plus, H.264와 색 변환(설계 §3, §4).
4. 빌드 옵션 `REPSP_CODEC_FFMPEG`와 FFmpeg 찾기(설계 §5).
5. `scripts/fetch_ffmpeg.py`와 `.gitignore`(설계 §6). 고정할 릴리스 태그와 SHA-256을 정해 표에 넣는다.
6. probe(설계 §7의 1~4).
7. 문서: `THIRD_PARTY_NOTICES.md`, `docs/guides/`(받는 방법, 빌드 옵션, 함께 배포할 때의 조건), `docs/kb/`(디코더 계약, 인코더 지연과 끝 처리, 색 변환; 확인 상태와 출처), `ARCHITECTURE.md`(타깃 표, 플러그인 계약), 작업 로그 `docs/work-logs/20261114-i021-codec-plugin.md`.
8. 검증: 설계 §7.

개발용 FFmpeg: Win64 LGPL 공유 빌드(n8.1)가 `%TEMP%\redmp1-build\deps\ffmpeg\` 아래에 풀려 있다. `FFMPEG_ROOT`로 쓸 수 있다. 이 디렉터리의 파일을 저장소에 복사하지 않는다.

지킬 것: FFmpeg의 구현 소스와 PPSSPP 코드를 읽거나 옮기지 않는다(공개 API 헤더와 공식 API 문서만). FFmpeg 헤더는 플러그인 타깃에서만 포함한다. FFmpeg 바이너리·헤더를 커밋하지 않는다. GPL 구성의 빌드를 쓰지 않는다. 기본 빌드에 FFmpeg 의존을 넣지 않는다. 타이틀 이름·주소를 코드·probe·문서에 쓰지 않는다. 게임 유래 데이터는 커밋하지 않는다(probe는 합성 데이터만). 문서와 로그에 사용자 계정 이름이 든 절대 경로를 쓰지 않는다. 작업 단위마다 커밋, 제목 끝에 `(#21)`. push·PR·merge·tag 금지.

작업 환경에서 git 커밋, WSL 실행, 네트워크가 막히면 우회하지 말고, 변경을 작업 트리에 둔 채 작업 로그와 보고에 그 사실을 적는다. Linux에서도 빌드되도록 GCC 경고(`-Wall -Wextra -Wpedantic -Werror`)를 미리 살핀다.

완료 기준: 설계 §7의 1~5.

---

# Work order: codec plugin (issue #21)

Design: [20261114-i021](../design/20261114-i021-codec-plugin.md)
Branch: `i021-codec-plugin` (from main `7c80806`), worked on in reDMP1's submodule `third_party/repsp`.

(1) The plugin contract and loader (design §2), with a dynamic library facility in the platform layer if needed. (2) Decoder contract changes: per-stream contexts and a widened `AtracFrameInfo` (design §3), with `repsp_hle_atrac` and `repsp_hle_mpeg` adapted. (3) The plugin `repsp_codec_ffmpeg`: ATRAC3 and ATRAC3plus, H.264 and colour conversion (design §3, §4). (4) The build option `REPSP_CODEC_FFMPEG` and locating FFmpeg (design §5). (5) `scripts/fetch_ffmpeg.py` and `.gitignore` (design §6), choosing the release tag and SHA-256 to pin. (6) Probes (design §7, 1-4). (7) Documents: `THIRD_PARTY_NOTICES.md`, `docs/guides/` (how to get FFmpeg, the build option, the conditions for shipping it alongside), `docs/kb/` (decoder contracts, encoder delay and end handling, colour conversion; confirmation status and sources), `ARCHITECTURE.md` (target table, plugin contract), the work log `docs/work-logs/20261114-i021-codec-plugin.md`. (8) Verify: design §7.

FFmpeg for development: a Win64 LGPL shared build (n8.1) is unpacked under `%TEMP%\redmp1-build\deps\ffmpeg\` and can be used as `FFMPEG_ROOT`; nothing from that directory is copied into the repository.

Must hold: FFmpeg's implementation source and PPSSPP code are not read or carried over (public API headers and the official API documentation only); FFmpeg headers are included only by the plugin target; no FFmpeg binary or header is committed; no GPL-configured build; no FFmpeg dependency in the default build; no title name or address in code, probes or documents; no game-derived data is committed (probes use synthetic data only); no absolute path containing a user account name in documents or logs. Commit per unit of work, titles ending in `(#21)`; no push, pull request, merge or tag.

If git commits, WSL or the network are blocked in the working environment, do not work around it: leave the changes in the working tree and say so in the work log and the report. Watch for GCC warnings (`-Wall -Wextra -Wpedantic -Werror`) so the Linux build passes.

Done when: design §7, items 1-5.
