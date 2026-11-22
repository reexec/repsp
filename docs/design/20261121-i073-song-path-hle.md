# 설계: 타이틀 화면에서 한 곡 플레이까지의 HLE (issue #73)

소비자 요구: [reexec/reDMP2#13](https://github.com/reexec/reDMP2/issues/13).

## 1. 출발점 (확인됨)

* repsp v0.0.33에서 소비자가 타이틀 화면에 닿는다. START를 누르면 모드 선택 화면이 표시된다.
* 모드를 고르면 로딩 화면으로 넘어간다. 거기서 `ThreadManForUser` `sceKernelGetSystemTime`(`0xDB738F35`) 미구현으로 멈춘다. 1,178프레임째, 2,456 vblank다.
* 소비자의 정적 대조로는 미구현 NID가 68개 남아 있다.

## 2. 방침

* [#71 설계](20261121-i071-title-path-hle.md)의 방침을 그대로 쓴다:
  * 측정이 이끄는 반복
  * 출처 있는 반환값만
  * 타이틀 중립
  * PPSSPP 코드 금지
  * 의미가 분명한 같은 API 묶음의 이웃 함수는 함께 구현할 수 있다
* `sceKernelGetSystemTime`(`SceKernelSysClock*`, 64비트 µs)은 기존 시계 함수(`sceKernelGetSystemTimeWide`, `sceKernelGetSystemTimeLow`)와 같은 시간원을 쓴다. 같은 묶음의 변환 함수(`sceKernelSysClock2USec` 등)는 불릴 때 구현한다.
* **멈춤 기준(완료 조건)**: 다음 중 하나에 닿으면 끝낸다.
  * 한 곡이 끝나 결과 화면이 표시된다. 클리어와 실패를 가리지 않는다.
  * 미구현 NID가 아닌 이유로 멈춘다.

  디스패치 미스는 먼저 lift 누락인지(코드가 조립하는 포인터) 확인하고, 근거와 함께 보고한다. lift 쪽은 소비자의 일이다.
* 입력: 소비자의 `--input-script`로 타이틀, 모드 선택, 곡 선택, 플레이를 진행한다. 노트를 맞추는 입력은 이 issue의 목표가 아니다. 실패해서 결과 화면에 닿아도 된다. 스크립트는 작업 로그에 남긴다.
* 오디오·영상 경로(`sceSas`, `sceAtrac`, `sceMpeg`, 스트림)의 동작 오류는 화면·로그로 기록한다. 정지 원인이 아니면 별도 issue 후보로 적는다.

## 3. 검증

1. 각 구현의 probe(CTest, `TIMEOUT`)가 통과하고, 기존 probe도 전부 통과한다.
2. Win64 Debug/Release 경고 0. Linux는 CI.
3. 작업 로그에 다음을 시간순으로 남긴다:
   * 소비자 측정 반복(멈춘 NID와 구현한 것)
   * 마지막 상태(결과 화면이면 그 프레임 번호와 화면 설명)
   * 정적 대조 수치 변화

---

# Design: HLE from the title screen to one played song (issue #73)

Consumer need: [reexec/reDMP2#13](https://github.com/reexec/reDMP2/issues/13).

## 1. Starting point (confirmed)

* With repsp v0.0.33 the consumer reaches the title screen; START shows the mode select screen.
* Choosing a mode moves to the loading screen, which stops on unimplemented `ThreadManForUser` `sceKernelGetSystemTime` (`0xDB738F35`) at frame 1,178, vblank 2,456.
* The consumer's static match still lists 68 unimplemented NIDs.

## 2. Approach

* The approach of the [#71 design](20261121-i071-title-path-hle.md) carries over:
  * a measured loop
  * sourced return values only
  * title-neutral code
  * no PPSSPP code
  * clear siblings in the same API family may be implemented together
* `sceKernelGetSystemTime` (`SceKernelSysClock*`, 64-bit µs) uses the same time source as the existing clock functions (`sceKernelGetSystemTimeWide`, `sceKernelGetSystemTimeLow`). Conversion siblings (`sceKernelSysClock2USec`, ...) are implemented when called.
* **Stopping rule (done criterion)**: the issue ends at the first of these:
  * one song ends and its result screen is presented, whether cleared or failed
  * the run stops for a reason other than an unimplemented NID

  A dispatch miss is first checked for a lift miss (a code-built pointer) and reported with evidence. The lift side is the consumer's job.
* Input: the consumer's `--input-script` drives the title, mode select, song select and play. Hitting notes is not a goal here; reaching the result screen by failing is fine. The script goes into the work log.
* Misbehaviour on audio and video paths (`sceSas`, `sceAtrac`, `sceMpeg`, streams) is recorded from the screen and logs. If it does not stop the run, it is listed as a candidate for a separate issue.

## 3. Verification

1. A probe per implementation (CTest, `TIMEOUT`); all existing probes pass.
2. Win64 Debug/Release with zero warnings; Linux in CI.
3. The work log records, in order:
   * the consumer-side loop (the NID it stopped on, what was implemented)
   * the final state (for a result screen, its frame number and a description)
   * the change in static coverage numbers
