# 설계: 소리 결함 — 반복되는 효과음, 느리게 나는 소리 (issue #26)

## 1. 요구사항

* 소비자([reDMP1 issue #26](https://github.com/reexec/reDMP1/issues/26))에서 사용자가 귀로 확인한 결함이다. 동영상의 소리는 맞다. 그 밖의 소리는 맞는 것과 틀린 것이 섞여 있다: 메뉴 화면에서 효과음 하나가 계속 반복되고, 일부 소리가 느리게 난다.
* 원인을 증거로 확정한 뒤 고친다. 추측으로 고치지 않는다.

## 2. 조사할 경로와 가설

* **SAS 음성(`repsp_hle_sascore`)**
  * 반복: `__sceSasSetVoice`의 loop 인자와 VAG 블록 플래그(끝 `0x01`, 반복 `0x02`, 반복 시작 `0x04`, 끝·무음 `0x07`)의 관계. 지금 구현은 loop 인자가 0이어도 블록의 반복 플래그만으로 되돌아간다. 실제 동작(loop 인자가 0이면 반복 플래그가 있어도 끝나는지)을 공개 자료로 확인한다.
  * 끝 보고: `__sceSasGetEndFlag`, 일시 정지 플래그, envelope이 0에 이른 음성의 처리, key off 뒤 release가 끝난 음성의 상태. 끝났다고 보고되지 않으면 타이틀이 음성을 회수하지 못하거나 다시 건다.
  * 속도: pitch의 기준(`0x1000`이 44100 Hz인지), 보간 위치 계산, grain과 출력 표본화 주파수(`__sceSasInit`의 인자, `__sceSasSetOutputmode`)의 반영.
* **ATRAC(`repsp_hle_atrac`, 코덱 플러그인)**
  * 속도: 표본화 주파수와 채널 수가 44100 Hz stereo가 아닌 스트림(mono, 22050 Hz, ATRAC3와 ATRAC3plus의 차이)의 처리. 디코딩 결과의 채널 배치와 `sceAtracDecodeData`가 돌려주는 샘플 수, 타이틀이 `sceAudio`에 넘기는 샘플 수가 서로 맞는지.
  * 반복: 루프 정보(RIFF `smpl` 청크), `sceAtracGetLoopStatus`, `sceAtracSetLoopNum`, 끝에 이른 스트림의 반환값(`remainFrame`, 끝 오류 코드).
* **sceAudio 믹서(`repsp_hle_audio`)**
  * 채널 형식(mono/stereo)과 샘플 수(`sceAudioSetChannelDataLen`, 예약 때의 값)의 반영. mono 채널을 stereo로, 또는 그 반대로 다루면 속도가 2배 달라진다.
  * 출력 대기의 속도(소비 속도에 맞춘 대기)가 채널 형식에 따라 맞는지.

## 3. 증거를 얻는 수단

* 진단 기능(환경 변수로 켠다; 꺼져 있을 때 비용이 없다):
  * `REPSP_AUDIO_DUMP`의 확장: sceAudio 채널별 WAV(믹서 입력), SAS 믹서 출력의 WAV.
  * SAS 음성 추적: `SetVoice`(주소, 크기, loop), 만난 블록 플래그, key on/off, pitch, 끝 보고의 변화를 한 줄씩.
  * ATRAC 추적: 스트림의 형식(코덱, 채널, 표본화 주파수, block align, 루프 지점), 디코딩 호출마다의 샘플 수와 끝 상태.
* 기준(oracle): 같은 RIFF 데이터를 FFmpeg 명령줄 도구로 디코딩한 결과와 길이·채널·표본화 주파수를 비교한다(동작 비교용; 코드는 보지 않는다). SAS의 VAG는 블록 수와 pitch로 계산한 재생 시간과 실제 출력 시간을 비교한다.
* 사람의 귀가 최종 판정이다. 고친 뒤 사용자가 창 모드로 확인한다.

## 4. 수정 원칙

* 실제 동작을 공개 자료(PSPSDK 헤더, psdevwiki, uOFW)로 확인하고, KB의 해당 항목을 **확인됨** 또는 **추정**으로 갱신한다.
* 모든 수정에는 그 결함을 다시 잡는 probe를 더한다(합성 VAG, 합성 RIFF 머리).
* 스케줄러의 의미와 소비자 계약은 바꾸지 않는다. 바꿔야 한다면 질문으로 올린다.
* 타이틀의 이름이나 주소를 코드·probe·문서에 쓰지 않는다.

## 5. 이번 task의 검증

1. 결함마다 원인을 한 문장으로 적고, 그것을 잡는 probe가 있으며 통과한다.
2. 기존 probe가 그대로 통과한다.
3. Win64 Debug/Release, Linux x64 Debug/Release 빌드와 CTest.

---

# Design: sound defects — a sound effect that repeats, sounds that play slowly (issue #26)

## 1. Requirements

Defects the user confirmed by ear in the consumer ([reDMP1 issue #26](https://github.com/reexec/reDMP1/issues/26)): movie audio is right; other sounds are a mix of right and wrong — one sound effect keeps repeating on a menu screen and some sounds play slowly. Causes are confirmed by evidence before fixing; no fixing by guesswork.

## 2. Paths to examine and hypotheses

**SAS voices (`repsp_hle_sascore`)**. Repetition: the relation between the loop argument of `__sceSasSetVoice` and the VAG block flags (end `0x01`, repeat `0x02`, loop start `0x04`, end-and-mute `0x07`); the present code jumps back on the block's repeat flag alone even when the loop argument is 0, and the real behaviour (whether a zero loop argument ends the voice despite a repeat flag) is to be confirmed from public sources. End reporting: `__sceSasGetEndFlag`, the pause flag, voices whose envelope reached zero, and the state of a voice after key off once its release has finished — a voice never reported as ended cannot be reclaimed by the title, or is triggered again. Speed: the pitch base (whether `0x1000` is 44100 Hz), the interpolation position, and how grain and the output sample rate (arguments of `__sceSasInit`, `__sceSasSetOutputmode`) are honoured.

**ATRAC (`repsp_hle_atrac`, the codec plugin)**. Speed: streams that are not 44100 Hz stereo (mono, 22050 Hz, ATRAC3 versus ATRAC3plus) — whether the decoded channel layout, the sample count returned by `sceAtracDecodeData` and the sample count the title passes to `sceAudio` agree. Repetition: loop information (the RIFF `smpl` chunk), `sceAtracGetLoopStatus`, `sceAtracSetLoopNum`, and the values returned at the end of a stream (`remainFrame`, the end error code).

**The sceAudio mixer (`repsp_hle_audio`)**. Whether the channel format (mono/stereo) and sample count (`sceAudioSetChannelDataLen`, the value at reservation) are honoured — treating a mono channel as stereo or the reverse changes the speed by a factor of two — and whether the pacing of blocking output is right for each format.

## 3. Means of evidence

Diagnostics switched on by environment variables, free when off: an extension of `REPSP_AUDIO_DUMP` giving one WAV per sceAudio channel (mixer input) and a WAV of the SAS mixer output; a SAS voice trace, one line each for `SetVoice` (address, size, loop), block flags met, key on/off, pitch and changes in end reporting; an ATRAC trace giving a stream's format (codec, channels, sample rate, block align, loop points) and, per decode call, the sample count and end state. Oracle: the same RIFF data decoded with the FFmpeg command-line tool, compared for length, channels and sample rate (behaviour comparison only; no code is read); for SAS, the playing time computed from the VAG block count and pitch against the time actually output. The human ear is the final judge: after the fixes the user checks in windowed mode.

## 4. Rules for fixes

Real behaviour is confirmed from public sources (PSPSDK headers, psdevwiki, uOFW) and the knowledge base entry updated as **confirmed** or **assumed**. Every fix comes with a probe that catches the defect again (a synthetic VAG, a synthetic RIFF header). Scheduler semantics and consumer contracts are not changed; if a change is needed it is raised as a question. No title name or address in code, probes or documents.

## 5. Verification for this task

1. For each defect the cause is stated in one sentence and a probe catches it and passes.
2. Existing probes pass unchanged.
3. Win64 Debug/Release and Linux x64 Debug/Release builds and CTest.
