# 지식 기반 색인

PSP 일반 지식(컨테이너 형식, Allegrex·VFPU, 펌웨어 모듈과 NID, GE 명령, 오디오 형식)을 주제별로 둔다. 각 문서는 **확인됨**, **추정**, **미확정**을 구분하고 출처를 단다. 타이틀 고유 분석은 각 타이틀 프로젝트의 `docs/analysis/`에 있다.

| 문서 | 내용 | 상태 |
|---|---|---|
| [`psp-memory-map.md`](psp-memory-map.md) | PSP 물리 메모리 맵과 0x1FFFFFFF 가상 미러 접기 규칙 | **추정** (repsp 512 MiB 창 예약 및 접기는 확인됨) |
| [`param-sfo.md`](param-sfo.md) | `PARAM.SFO` 바이너리 헤더, 엔트리 테이블 및 데이터 형식 | **추정** (파서 구현 동작은 합성 데이터로 확인됨) |
| [`prx-format.md`](prx-format.md) | PSP PRX 바이너리 헤더, `SHT_PRXRELOC` 재배치 형식 및 모듈 정보·스터브 구조 | **확인됨** (신세틱 프로브 및 원본 바이너리 측정 검증) |
| [`nid.md`](nid.md) | PSP 펌웨어 모듈 NID(Name ID) 정의 및 SHA-1 계산 알고리즘 | **확인됨** (FIPS 180 벡터 및 공개 NID 대조) |
| [`scheduler.md`](scheduler.md) | PSP 단일 코어 고정 우선순위 스케줄러, 스택 인자 전달 및 콜백 규약 | **추정** (uOFW; `kernel_sched_probe` 합성 검증) |
| [`kernel-errors.md`](kernel-errors.md) | 커널 및 파일 입출력 오류 코드 표와 출처 | **추정** (uOFW, PSPSDK) |
| [`vfs-io.md`](vfs-io.md) | 장치 경로 정규화, 대소문자 무시 탐색, `PSP_O_*` 플래그 및 `sceIoLseek` 규약 | **추정** (PSPSDK; `iofile_probe` 합성 검증) |
| [`ge.md`](ge.md) | PSP GE 디스플레이 리스트, stall 주소, 정점·텍스처·CLUT 규약 | **추정** (PSPSDK, psprecomp; `ge_probe` 합성 검증) |
| [`display.md`](display.md) | PSP 디스플레이 픽셀 형식, `SetFrameBuf` 동기 모드, VBlank 동기화 | **추정** (PSPSDK, uOFW; `display_probe` 합성 검증) |
| [`ctrl-and-input.md`](ctrl-and-input.md) | PSP 컨트롤러 입력(`sceCtrl`·`SceCtrlData`·링 버퍼·입력 매핑) 및 `sceDmacMemcpy` | **추정** (PSPSDK, uOFW; `vblank_probe`·`ctrl_probe`·`dmac_probe` 합성 검증) |
| [`audio.md`](audio.md) | PSP 오디오 출력(`sceAudio`), 채널·형식·이중 버퍼 대기, 끌어가기(pull) 믹서·시계 | **추정** (PSPSDK, uOFW; `platform_probe`·`audio_probe` 합성 검증) |
| [`sascore-and-vag.md`](sascore-and-vag.md) | `sceSasCore` 신시사이저, Sony VAG 4비트 ADPCM 블록·필터, ADSR, Dry-only 리버브 | **추정** (PSDevWiki, `psx-spx`; `sas_probe` 합성 검증) |
| [`atrac3plus.md`](atrac3plus.md) | RIFF/WAVE ATRAC3·ATRAC3plus 컨테이너, `sceAtrac3plus` 상태 머신, 교체형 `AtracDecoder` | **추정** (PSPSDK `pspatrac3.h`, RIFF 규격; `atrac_probe` 합성 검증) |

---

# Knowledge base index

General PSP knowledge (container formats, the Allegrex and VFPU, firmware modules and NIDs, GE commands, audio formats), by topic. Each document marks **confirmed**, **inferred** and **unresolved** statements and cites its sources. Title-specific analysis lives in each title project's `docs/analysis/`.

| Document | Contents | Status |
|---|---|---|
| [`psp-memory-map.md`](psp-memory-map.md) | PSP physical memory map and 0x1FFFFFFF virtual mirror folding | **Inferred** (repsp 512 MiB window and folding confirmed) |
| [`param-sfo.md`](param-sfo.md) | `PARAM.SFO` binary header, entry table, and data formats | **Inferred** (parser behavior confirmed via synthetic probe) |
| [`prx-format.md`](prx-format.md) | PSP PRX binary header, `SHT_PRXRELOC` relocation format, and module info/stub layout | **Confirmed** (verified via synthetic probe and disc binary measurements) |
| [`nid.md`](nid.md) | PSP firmware module NID definition and SHA-1 calculation | **Confirmed** (FIPS 180 vectors and published NIDs verified) |
| [`scheduler.md`](scheduler.md) | PSP single-core fixed-priority scheduler, stack argument layout, and callbacks | **Inferred** (uOFW; synthetic `kernel_sched_probe` verified) |
| [`kernel-errors.md`](kernel-errors.md) | Kernel and I/O error code table with sources | **Inferred** (uOFW, PSPSDK) |
| [`vfs-io.md`](vfs-io.md) | Device path normalization, case-insensitive lookup, `PSP_O_*` flags, `sceIoLseek` ABI | **Inferred** (PSPSDK; synthetic `iofile_probe` verified) |
| [`ge.md`](ge.md) | PSP GE display lists, stall address, vertex/texture/CLUT conventions | **Inferred** (PSPSDK, psprecomp; synthetic `ge_probe` verified) |
| [`display.md`](display.md) | PSP display pixel formats, `SetFrameBuf` sync modes, VBlank sync | **Inferred** (PSPSDK, uOFW; synthetic `display_probe` verified) |
| [`ctrl-and-input.md`](ctrl-and-input.md) | PSP controller input (`sceCtrl`, `SceCtrlData`, ring buffer, host mapping) and `sceDmacMemcpy` | **Inferred** (PSPSDK, uOFW; synthetic `vblank_probe`, `ctrl_probe`, `dmac_probe` verified) |
| [`audio.md`](audio.md) | PSP audio output (`sceAudio`), channels/formats/double-buffered waits, pull mixer & host pacing | **Inferred** (PSPSDK, uOFW; synthetic `platform_probe`, `audio_probe` verified) |
| [`sascore-and-vag.md`](sascore-and-vag.md) | `sceSasCore` synthesizer, Sony VAG 4-bit ADPCM blocks/filters, ADSR, dry-only reverb | **Inferred** (PSDevWiki, `psx-spx`; synthetic `sas_probe` verified) |
| [`atrac3plus.md`](atrac3plus.md) | RIFF/WAVE ATRAC3/ATRAC3plus container, `sceAtrac3plus` state machine, replaceable `AtracDecoder` | **Inferred** (PSPSDK `pspatrac3.h`, RIFF spec; synthetic `atrac_probe` verified) |
