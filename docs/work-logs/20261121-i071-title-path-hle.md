# 작업 로그: 오프닝 영상에서 타이틀 화면까지의 부팅 경로 HLE (issue #71)

* 이슈: [#71](https://github.com/reexec/repsp/issues/71) (소비자 요구: [reexec/reDMP2#9](https://github.com/reexec/reDMP2/issues/9))
* 설계: [20261121-i071-title-path-hle.md](../design/20261121-i071-title-path-hle.md)
* 작업 지시: [20261121-i071-title-path-hle.md](../work-orders/20261121-i071-title-path-hle.md)
* 브랜치: `i71-title-path-hle`

---

## 1. 측정 반복 기록 (소비자: [reexec/reDMP2#9](https://github.com/reexec/reDMP2/issues/9))

실행 환경: 소비자 Release 빌드 + 로컬 FFmpeg LGPL 코덱 플러그인(읽기 전용 경로) + `--headless` + `--input-script` + `REPSP_FRAME_DUMP`(저장소 밖 임시 디렉터리).

| 반복 | 정지 원인 / 관찰 | 구현 및 조치 |
|---:|---|---|
| 1 | `IoFileMgrForUser:sceIoCloseAsync` (`0xFF5940B6`) 미구현 | `src/hle/kernel/iofilemgr.cpp`에 `sceIoCloseAsync` 구현 (`closing_async` 플래그로 `WaitAsync*`/`PollAsync` 회수 시 `fd` 제거), `iofile_probe` 검증 추가 (`63cd201`) |
| 2 | 소비자 측 디스패치 미스(리프트 누락 확인 → 소비자 #9로 분리 보고) | 소비자 측 리프트 패치 적용 후 재실행 → 다음 정지 `sceRtc:sceRtcGetCurrentClockLocalTime` (`0xE7C27D1B`) |
| 3 | `sceRtc:sceRtcGetCurrentClockLocalTime` (`0xE7C27D1B`) 미구현 | `src/hle/kernel/rtc.cpp`에 `sceRtcGetCurrentClockLocalTime` 구현(PSPSDK `ScePspDateTime` 16바이트, null 시 `0x80510002`), `sysmem_probe` 검증 추가 (`4e5fcd2`). 재실행 시 저장 데이터 없음 확인 다이얼로그 통과 후 언어 선택 화면(`LANGUAGE SELECT`) 표시(868프레임, 오류 0) |
| 4–6 | 입력 스크립트 조정(언어 선택 → 초기 프로필 생성 화면 → 이름 1글자 입력 후 `END` 선택 → 초기 질문 응답) | 프로필 자동 저장(`AUTOSAVE`)까지 진행, 직후 `IoFileMgrForUser:sceIoGetstat` (`0xACE946E8`) 미구현으로 정지 |
| 7 | `IoFileMgrForUser:sceIoGetstat` (`0xACE946E8`) 미구현 | `src/hle/kernel/iofilemgr.cpp`에 `sceIoGetstat` 구현(PSPSDK `SceIoStat` 88바이트, `FIO_S_IFDIR`/`FIO_S_IFREG`, `FIO_SO_IFDIR`/`FIO_SO_IFREG`, `st_size`), `iofile_probe` 검증 추가 (`5f6afb0`). 재실행 시 **타이틀 화면 도달 및 입력 대기 상태 확인** |

---

## 2. 최종 상태 및 입력 스크립트 (**확인됨**)

### 사용한 입력 스크립트 (`--max-vblanks 2400`)
```text
600 START
610 NONE
750 CIRCLE
760 NONE
950 CIRCLE
960 NONE
1100 CIRCLE
1110 NONE
1180 UP
1190 NONE
1260 CIRCLE
1270 NONE
1360 CIRCLE
1370 NONE
1460 RIGHT
1470 NONE
1520 CIRCLE
1530 NONE
1640 CIRCLE
1650 NONE
```

### 최종 상태
* **정지 분류 / 종료 코드**: `(e) VBlank limit reached (--max-vblanks)` / `0` (새 미구현 NID 없음, 디스패치 미스 `0`).
* **표시 프레임**: 총 `1,101` 프레임 표시 (`vblanks=2400`, `interval mean=36.35 ms`), GE `807` 리스트 / `191,994` 정점 / `297,405,240` 픽셀 출력, SAS 보이스 및 스트림 BGM 정상 동작.
* **타이틀 화면 확인** (`frame_1050.bmp` ~ `frame_1100.bmp`, 저장소 밖 임시 덤프 확인 후 커밋하지 않음):
  * 붉은 조명과 스피커 배경의 콘서트 무대 위에 은색 날개형 엠블럼과 중앙 원형 배지가 상단에 표시되고, 중앙 하단에 좌우 화살표와 대형 타이틀 로고, 그 아래 `PRESS START BUTTON` 안내 문구와 최하단 저작권 표시 바가 선명하게 렌더링된 채 입력을 대기하고 있습니다.

---

## 3. 정적 커버리지 변화 (소비자 기준)

* `repsp` 등록 HLE NID 수: `168` → **`171`** (`+3`: `sceIoCloseAsync`, `sceRtcGetCurrentClockLocalTime`, `sceIoGetstat`).
* 소비자 정적 대조: `158 / 229` (미구현 `71`) → **`161 / 229` (`70.3%`, 미구현 `68`)** (`IoFileMgrForUser` 13/24, `sceRtc` 1/1).

---

## 4. 검증 결과 (`repsp` 단독)

* **Win64 Debug**: 경고 **0건**, CTest **23/23 통과** (`iofile_probe`, `sysmem_probe` 포함).
* **Win64 Release**: 경고 **0건**, CTest **23/23 통과**.

---

# Work Log: Boot-Path HLE from Opening Movie to Title Screen (issue #71)

* Issue: [#71](https://github.com/reexec/repsp/issues/71) (consumer need: [reexec/reDMP2#9](https://github.com/reexec/reDMP2/issues/9))
* Design: [20261121-i071-title-path-hle.md](../design/20261121-i071-title-path-hle.md)
* Work order: [20261121-i071-title-path-hle.md](../work-orders/20261121-i071-title-path-hle.md)
* Branch: `i71-title-path-hle`

---

## 1. Measured Loop (Consumer: [reexec/reDMP2#9](https://github.com/reexec/reDMP2/issues/9))

1. **`IoFileMgrForUser:sceIoCloseAsync` (`0xFF5940B6`)** (`63cd201`): Implemented in `src/hle/kernel/iofilemgr.cpp` using the existing async result model (`closing_async` keeps the fd until `WaitAsync*`/`PollAsync` collects result `0`); tested in `iofile_probe`.
2. **Consumer lift miss (handed back to consumer #9)**: After consumer patch resolved the indirect callback miss, boot stopped at `sceRtc:sceRtcGetCurrentClockLocalTime` (`0xE7C27D1B`).
3. **`sceRtc:sceRtcGetCurrentClockLocalTime` (`0xE7C27D1B`)** (`4e5fcd2`): Implemented in `src/hle/kernel/rtc.cpp` (PSPSDK 16-byte `ScePspDateTime`, returning `0x80510002` on null); tested in `sysmem_probe`. Re-run passed savedata/msgdialog and reached the language selection screen (`868` frames, exit `0`).
4. **Input script progression (language select → profile creation → initial prompt → autosave)**: Stopped at `IoFileMgrForUser:sceIoGetstat` (`0xACE946E8`).
5. **`IoFileMgrForUser:sceIoGetstat` (`0xACE946E8`)** (`5f6afb0`): Implemented in `src/hle/kernel/iofilemgr.cpp` (PSPSDK 88-byte `SceIoStat` with `FIO_S_IFDIR`/`FIO_S_IFREG`, `FIO_SO_IFDIR`/`FIO_SO_IFREG`, `st_size`); tested in `iofile_probe`. Re-run reached the **title screen waiting for input**.

---

## 2. Final State & Input Script (**confirmed**)

* Input script (see Korean section above) with `--max-vblanks 2400`: clean exit `(e) VBlank limit reached` (exit code `0`, `0` dispatches, no new unimplemented NID), `1,101` frames presented (`mean=36.35 ms`), `807` GE lists / `191,994` vertices / `297,405,240` pixels written.
* Title screen confirmed in `frame_1050.bmp`–`frame_1100.bmp` (inspected outside the repo, never committed): red-lit concert stage background with speaker walls, silver winged emblem with central circular badge at top, metallic title logo flanked by arrows, `PRESS START BUTTON` prompt waiting for input, and copyright footer bar.

---

## 3. Static Coverage & Verification

* Registered `repsp` HLE NIDs: `168` → **`171`** (`+3`). Consumer static coverage: `158/229` (`71` missing) → **`161/229` (`70.3%`, `68` missing)**.
* **Win64 Debug standalone**: **0 warnings**, **23/23 CTest passed**.
* **Win64 Release standalone**: **0 warnings**, **23/23 CTest passed**.
