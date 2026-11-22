# 작업 로그: 타이틀 화면에서 한 곡 플레이까지의 HLE (issue #73)

* 이슈: [#73](https://github.com/reexec/repsp/issues/73) (소비자 요구: [reexec/reDMP2#13](https://github.com/reexec/reDMP2/issues/13))
* 설계: [20261121-i073-song-path-hle.md](../design/20261121-i073-song-path-hle.md)
* 작업 지시: [20261121-i073-song-path-hle.md](../work-orders/20261121-i073-song-path-hle.md)
* 브랜치: `i73-song-path-hle`

---

## 1. 측정 반복 기록 (소비자: [reexec/reDMP2#13](https://github.com/reexec/reDMP2/issues/13))

실행 환경: 소비자 Release FFmpeg 플러그인 빌드 + `--headless` + `--input-script` + `--save-dir`/`--crash-dir`(저장소 밖 임시 디렉터리) + `REPSP_FRAME_DUMP`(저장소 밖 임시 디렉터리).

| 반복 | 정지 원인 / 관찰 | 구현 및 조치 |
|---:|---|---|
| 1 | 출발점: 모드 선택 후 로딩 화면에서 `ThreadManForUser:sceKernelGetSystemTime` (`0xDB738F35`) 미구현으로 정지 | `src/hle/kernel/threadman_thread.cpp`에 `sceKernelGetSystemTime` 구현(`GetSystemTimeWide()` 64비트 µs를 `SceKernelSysClock`의 `low`/`hi`에 기록, null 시 `kSceKernelErrorIllegalAddr` `0x800200D6`), `kernel_sched_probe` 항목 19 추가. 재실행 시 다음 정지 `ThreadManForUser:sceKernelSysClock2USec` (`0xBA6B92E2`, 1,129프레임 / 2,458 vblank) |
| 2 | `ThreadManForUser:sceKernelSysClock2USec` (`0xBA6B92E2`) 미구현 | `src/hle/kernel/threadman_thread.cpp`에 `sceKernelSysClock2USec` 구현(64비트 µs를 초 `total_us / 1000000`과 나머지 마이크로초 `total_us % 1000000`으로 분할 기록, 선택적 null 출력 포인터 허용, `clock == 0` 시 `kSceKernelErrorIllegalAddr`), `kernel_sched_probe` 항목 19에 64비트 분할·널 포인터 검증 추가 (`d1b7b55`). 재실행(`--max-vblanks 3600`)에서 **곡 선택 화면 도달** (`1,447`프레임, 종료 코드 `0`, 디스패치 미스 `0`) |
| 3 | 곡 선택 입력(`2900 CIRCLE`, `3100 CIRCLE`)을 추가해 `--max-vblanks 9000` 실행 | **곡 플레이 진입 → 게이지 소진 → 실패 결과(`GAMEOVER`) 화면 도달** (`2,977`프레임, 종료 코드 `0`, 새 미구현 NID `0`, 디스패치 미스 `0`) |
| 4 | 실패 결과 화면에서 `EXIT` 선택(`6000 DOWN`, `6100 DOWN`, `6200 CIRCLE`, `--max-vblanks 7500`) | 플레이 스레드 정상 종료 후 타이틀 화면 복귀 확인 (`2,594`프레임, 종료 코드 `0`, 새 미구현 NID `0`, 디스패치 미스 `0`) |

---

## 2. 최종 상태 및 입력 스크립트 (**확인됨**)

### 사용한 입력 스크립트 (반복 3: 곡 플레이 및 실패 결과 화면, `--max-vblanks 9000`)
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
1800 START
1810 NONE
2100 CIRCLE
2110 NONE
2400 CIRCLE
2410 NONE
2900 CIRCLE
2910 NONE
3100 CIRCLE
3110 NONE
```

### 최종 상태 (반복 3)
* **정지 분류 / 종료 코드**: `(e) VBlank limit reached (--max-vblanks)` / `0` (새 미구현 NID `0`, 디스패치 미스 `0`).
* **표시 프레임 / GE / 오디오**: 총 `2,977`프레임 표시 (`vblanks=9000`, `interval mean=50.45 ms`, `max=517.27 ms`), GE `2,683` 리스트 / `486,454` 정점 / `1,243,907,247` 픽셀 출력, SAS 보이스(`__sceSasCore` `16,737`회) 및 ATRAC3plus 스트림 디코딩(`2,130`프레임) 동작.
* **화면 확인** (저장소 밖 임시 BMP 덤프 확인 후 커밋하지 않음):
  * 곡 선택 화면(`frame_1446.bmp`, 반복 2): 좌측 곡 아트워크·점수 바·장비 미리보기, 우측 난이도(`NM`/`HD`/`MX`) 및 곡 목록이 표시됨.
  * 곡 플레이 화면(`frame_1600.bmp`, `frame_1750.bmp`, `frame_1850.bmp`): 배경 영상 재생, 중앙 4레인 기어·하강 노트·`BREAK` 판정 표시, 우측 체력 게이지가 점차 소진됨.
  * 실패 결과 화면(`frame_1900.bmp` ~ `frame_2976.bmp`): 상단에 `GAMEOVER / PRACTICE MAKES PERFECT` 배너, 중앙에 `RESTART` / `MUSIC SELECT` / `EXIT` 선택 메뉴, 하단에 저작권 표시 바가 선명하게 표시되며 입력을 대기함.

### 관찰된 이상 (별도 이슈 후보)
* **곡 선택 화면 우측 곡 목록 텍스트 깨짐**: 곡 선택 화면(`frame_1446.bmp`) 우측 곡 목록의 텍스트가 수평으로 깨져 표시됨. GE 텍스처 형식·스위즐 또는 CLUT/블렌드 처리의 별도 렌더링 이슈 후보.
* **오디오 언더런**: 소프트웨어 렌더러가 평균 ~50 ms/프레임으로 동작하는 구간에서 오디오 채널 언더런(`underruns=5096`, `host_ur=5651`)이 보고됨. 정지 원인은 아니며 성능·렌더러 최적화와 함께 추적할 항목.

---

## 3. 정적 커버리지 변화 (소비자 기준)

* `repsp` 등록 HLE NID 수: `171` → **`173`** (`+2`: `sceKernelGetSystemTime`, `sceKernelSysClock2USec`).
* 소비자 정적 대조: `161 / 229` (미구현 `68`) → **`163 / 229` (`71.2%`, 미구현 `66`)** (`ThreadManForUser` `33 / 33`, 미구현 `0`).

---

## 4. 검증 결과 (`repsp` 단독)

* **Win64 Debug**: 프로젝트 소스 경고 **0건**, CTest **23/23 통과** (`kernel_sched_probe` 항목 19 포함).
* **Win64 Release**: 프로젝트 소스 경고 **0건**, CTest **23/23 통과**.

---

# Work Log: HLE from the Title Screen to One Played Song (issue #73)

* Issue: [#73](https://github.com/reexec/repsp/issues/73) (consumer need: [reexec/reDMP2#13](https://github.com/reexec/reDMP2/issues/13))
* Design: [20261121-i073-song-path-hle.md](../design/20261121-i073-song-path-hle.md)
* Work order: [20261121-i073-song-path-hle.md](../work-orders/20261121-i073-song-path-hle.md)
* Branch: `i73-song-path-hle`

---

## 1. Measured Loop (Consumer: [reexec/reDMP2#13](https://github.com/reexec/reDMP2/issues/13))

1. **`ThreadManForUser:sceKernelGetSystemTime` (`0xDB738F35`)**: Implemented in `src/hle/kernel/threadman_thread.cpp` writing `GetSystemTimeWide()` (64-bit µs) into `SceKernelSysClock` (`low`, `hi`) and returning `kSceKernelErrorIllegalAddr` (`0x800200D6`) on null; tested in `kernel_sched_probe` item 19. Re-run stopped immediately at `ThreadManForUser:sceKernelSysClock2USec` (`0xBA6B92E2`, frame `1,129`, vblank `2,458`).
2. **`ThreadManForUser:sceKernelSysClock2USec` (`0xBA6B92E2`)** (`d1b7b55`): Implemented in `src/hle/kernel/threadman_thread.cpp` splitting the 64-bit microsecond `SceKernelSysClock` into seconds (`total_us / 1000000`) and remaining microseconds (`total_us % 1000000`), allowing optional null output pointers and returning `kSceKernelErrorIllegalAddr` when `clock == 0`; extended `kernel_sched_probe` item 19 with 64-bit split and null-pointer checks. Re-run (`--max-vblanks 3600`) reached the **song select screen** (`1,447` frames, exit `0`, `0` dispatch misses).
3. **Song selection and play (`2900 CIRCLE`, `3100 CIRCLE`, `--max-vblanks 9000`)**: Entered gameplay, played until health depleted, and reached the **failure result (`GAMEOVER`) screen** (`2,977` frames, exit `0`, `0` new unimplemented NIDs, `0` dispatch misses).
4. **Post-song transition check (`6000 DOWN`, `6100 DOWN`, `6200 CIRCLE` for `EXIT`, `--max-vblanks 7500`)**: Playback thread exited cleanly and returned to the title screen (`2,594` frames, exit `0`, `0` new unimplemented NIDs, `0` dispatch misses).

---

## 2. Final State, Input Script, and Observed Anomalies (**confirmed**)

* Input script: see Korean section above (`--max-vblanks 9000`).
* Final state (iteration 3): `(e) VBlank limit reached`, exit code `0`, `0` dispatches, `2,977` frames presented (`interval mean=50.45 ms`, `max=517.27 ms`), `2,683` GE lists / `486,454` vertices / `1,243,907,247` pixels written, SAS (`16,737` calls) and ATRAC3plus (`2,130` decoded frames) active.
* Visual verification (temporary BMP dumps outside the repo, never committed):
  * Song select (`frame_1446.bmp`, iter 2): jacket art, score bar, gear preview, difficulty selector (`NM`/`HD`/`MX`), and song list.
  * Gameplay (`frame_1600.bmp`, `frame_1750.bmp`, `frame_1850.bmp`): background video playing, 4-lane gear, falling notes, `BREAK` judgments, and depleting health gauge.
  * Failure result screen (`frame_1900.bmp`–`frame_2976.bmp`): `GAMEOVER / PRACTICE MAKES PERFECT` banner, `RESTART` / `MUSIC SELECT` / `EXIT` menu, and copyright footer waiting for input.
* Separate issue candidates observed (non-blocking):
  * **Garbled song-list text on song select**: Right-hand song-list text in `frame_1446.bmp` renders horizontally corrupted (likely a GE texture format/swizzle or CLUT/blend detail).
  * **Audio underruns**: Software rendering averages ~50 ms/frame during play, resulting in audio underruns (`underruns=5096`, `host_ur=5651`).

---

## 3. Static Coverage & Standalone Verification

* Registered `repsp` HLE NIDs: `171` → **`173`** (`+2`). Consumer static coverage: `161/229` (`68` missing) → **`163/229` (`71.2%`, `66` missing; `ThreadManForUser` `33/33`)**.
* **Win64 Debug standalone**: **0 project-source warnings**, **23/23 CTest passed**.
* **Win64 Release standalone**: **0 project-source warnings**, **23/23 CTest passed**.
