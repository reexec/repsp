# 작업 로그: 소리 결함 — 진단 기능 및 SAS 단발 효과음 반복 수정 (issue #26)

* 날짜: 2026-11-15
* 브랜치: `i026-sound-defects`
* 설계: [20261115-i026-sound-defects.md](../design/20261115-i026-sound-defects.md)
* 작업 지시: [20261115-i026-sound-defects.md](../work-orders/20261115-i026-sound-defects.md)

## 1. 진단 기능 구현

* **`REPSP_AUDIO_DUMP` 확장** (`src/hle/audio/hle_audio.cpp`, `src/hle/sascore/hle_sascore.cpp`):
  * 환경 변수 `REPSP_AUDIO_DUMP=<path>`가 설정되면 기존 최종 믹서 출력 WAV 외에, 실제 오디오 출력이 발생한 `sceAudio` 채널별 입력 WAV(`<stem>.ch<N>.wav`, 44100 Hz 2ch S16LE)와 `__sceSasCore` 믹서 출력 WAV(`<stem>.sas.wav`, 2ch S16LE)를 함께 기록한다.
  * 환경 변수가 비어 있거나 미설정이면 `bool` 플래그 분기만 수행하므로 비용이 없다.
* **SAS 음성 추적 (`REPSP_SAS_TRACE`)** (`src/hle/sascore/hle_sascore.cpp`):
  * `REPSP_SAS_TRACE=1` 설정 시 `Init`, `SetVoice`(주소, 크기, `startOff`, `loop`, 전체 VAG 블록의 `flags` 히스토그램), `KeyOn`, `KeyOff`, `SetPitch`, 첫 블록 및 `flags != 0` 블록, 종료 사유(`vag_block_end` / `env_release_zero`), `GetEndFlag` 비트마스크 변화를 `[SAS_TRACE]` 한 줄씩 출력한다.
* **ATRAC 추적 (`REPSP_ATRAC_TRACE`)** (`src/hle/atrac/hle_atrac.cpp`):
  * `REPSP_ATRAC_TRACE=1` 설정 시 `Open`(모드, 코덱 태그, 채널, 표본화 주파수, `block_align`, `spf`, `data_size`, `end_sample`, `delay`, `loop_start`, `loop_end`), `SetLoopNum`, `ResetPlayPosition`, `Decode` 호출별 샘플·끝 상태·잔여 프레임, `Release`를 `[ATRAC_TRACE]`로 출력한다.

## 2. 결함 원인 확정 및 수정

### 결함 1: 단발 VAG 효과음이 무한 반복되는 문제
* **증상**: 소비자 타이틀에서 모드 선택을 비롯한 모든 단발 효과음이 1회 재생 뒤 멈추지 않고 계속 반복된다.
* **증거**: 소비자 실행(`REPSP_SAS_TRACE=1`)에서 타이틀이 모든 효과음에 `__sceSasSetVoice(..., loop=1)`을 전달하며, 단발 효과음 VAG 블록은 `0x00` 데이터 블록 뒤에 `flags=0x01`(LoopEnd만 설정, LoopRepeat `0x02` 미설정)과 `0x07`(End+Mute)이 배치되어 있다.
* **원인**: `src/hle/sascore/hle_sascore.cpp`의 `DecodeNextVagBlock()`가 `kVagFlagLoopEnd`(`0x01`)를 만났을 때 `((flags & kVagFlagLoopRepeat) != 0 || v.loop)`로 OR 판정하여, `v.loop == true`이면 블록에 `0x02`가 없는 단발 `0x01` 블록에서도 `loop_start_offset`으로 되돌려 무한 반복시켰다.
* **수정**: 조건을 `((flags & kVagFlagLoopRepeat) != 0 && v.loop)`로 변경해 `__sceSasSetVoice`의 `loop` 인자가 0이 아니면서 블록에 `kVagFlagLoopRepeat`(`0x02`)가 설정된 경우(`0x03`)에만 루프백하고, `0x01` 단독 또는 `loop=0`에서는 `at_end_block = true`로 종료하도록 수정했다.
* **Probe**: `tools/sas_probe/main.cpp`에 5번(`flags=0x01` + `0x07` 패딩 VAG에 `loop=1` 전달 시 1회 종료 확인), 6번(`flags=0x04` -> `0x03` 루프 VAG에 `loop=0` 전달 시 미루프 종료 확인)을 추가해 6/6 통과를 확인했다.
* **KB 갱신**: `docs/kb/sascore-and-vag.md`에 PSX-SPX SPU ADPCM 규격 및 `sas_probe` 검증 기준의 **확인됨** 상태를 반영했다.

### 미해결 항목 (이번 범위 제외)
* **느리게 재생되는 소리**: 사용자 범위 조정에 따라 이번 작업에서는 수정하지 않는다. 다만 진단 중 소비자 ATRAC 스트림에서 `loopEnd=1039283664`가 관측되었으며, 추후 별도 작업에서 `smpl` 청크 파싱 및 재생 속도/포맷을 조사한다.

## 3. 검증 및 환경 제약

* Win64 Release (`Ninja`): 빌드 및 CTest 전체 통과(`sas_probe` 6/6 포함).
* `git commit` 및 Linux x64(`WSL`) 빌드·테스트는 샌드박스 환경 제약으로 실행하지 않았으며 변경 사항을 작업 트리에 두었다(리뷰어가 수행).

---

# Work Log: Sound Defects — Diagnostics and Fix for Repeating One-Shot SAS SFX (issue #26)

* Date: 2026-11-15
* Branch: `i026-sound-defects`
* Design: [20261115-i026-sound-defects.md](../design/20261115-i026-sound-defects.md)
* Work Order: [20261115-i026-sound-defects.md](../work-orders/20261115-i026-sound-defects.md)

## 1. Diagnostics Implemented

* **`REPSP_AUDIO_DUMP` extension** (`src/hle/audio/hle_audio.cpp`, `src/hle/sascore/hle_sascore.cpp`): when `REPSP_AUDIO_DUMP=<path>` is set, writes per-channel mixer input WAVs (`<stem>.ch<N>.wav`, 44100 Hz stereo S16LE) and the `__sceSasCore` mixer output WAV (`<stem>.sas.wav`) alongside the final mix WAV; zero overhead when unset.
* **SAS voice trace (`REPSP_SAS_TRACE=1`)** (`src/hle/sascore/hle_sascore.cpp`): logs `[SAS_TRACE]` lines for `Init`, `SetVoice` (including a VAG block flags histogram), `KeyOn`, `KeyOff`, `SetPitch`, first/non-zero-flag blocks, voice end reasons, and `GetEndFlag` transitions.
* **ATRAC trace (`REPSP_ATRAC_TRACE=1`)** (`src/hle/atrac/hle_atrac.cpp`): logs `[ATRAC_TRACE]` lines for `Open` (codec, channels, sample rate, block align, `spf`, `end_sample`, `delay`, loop points), `SetLoopNum`, `ResetPlayPosition`, per-call `Decode`, and `Release`.

## 2. Confirmed Defect & Fix

* **Symptom**: In the consumer title, one-shot sound effects (on mode select and across screens) repeat endlessly instead of stopping after playing once.
* **Evidence**: `REPSP_SAS_TRACE=1` shows the title calls `__sceSasSetVoice(..., loop=1)` on all sound effects while their VAG data ends with `flags=0x01` (`LoopEnd` without `LoopRepeat` `0x02`) followed by `0x07` (`End+Mute`).
* **Cause**: `DecodeNextVagBlock()` in `src/hle/sascore/hle_sascore.cpp` used `((flags & kVagFlagLoopRepeat) != 0 || v.loop)` when handling `kVagFlagLoopEnd` (`0x01`), so `v.loop == true` forced a jump back to `loop_start_offset` even on one-shot `0x01` blocks.
* **Fix**: Changed the condition to `((flags & kVagFlagLoopRepeat) != 0 && v.loop)` so looping occurs only when both `v.loop` is non-zero and block flag `0x02` (`kVagFlagLoopRepeat`) is set.
* **Probe**: Added checks 5 and 6 to `tools/sas_probe/main.cpp` (`0x01` + `0x07` with `loop=1` stops once; `0x04` -> `0x03` with `loop=0` does not loop); `6/6` checks pass.
* **KB update**: Updated `docs/kb/sascore-and-vag.md` with **Confirmed** status citing the PSX-SPX SPU ADPCM spec and `sas_probe`.
* **Unresolved (out of scope per user decision)**: Slow-playing sounds are deferred; during ATRAC tracing `loopEnd=1039283664` was observed and is recorded for follow-up.

## 3. Verification & Constraints

* Win64 Release (`Ninja`): build and CTest passed (`sas_probe` 6/6 included).
* `git commit` and Linux x64 (`WSL`) build/CTest were not run in the sandbox and are left in the working tree for the reviewer.
