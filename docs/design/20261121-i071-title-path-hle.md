# 설계: 오프닝 영상에서 타이틀 화면까지의 부팅 경로 HLE (issue #71)

소비자 요구: [reexec/reDMP2#9](https://github.com/reexec/reDMP2/issues/9).

## 1. 출발점 (확인됨)

* repsp v0.0.32 기준으로 소비자의 오프닝 영상(PMF)이 디코드·재생된다(선택적 FFmpeg 코덱 플러그인 사용 시).
* 입력 스크립트로 START를 눌러 영상을 건너뛰면 `IoFileMgrForUser` `sceIoCloseAsync`(`0xFF5940B6`) 미구현으로 멈춘다.
* 소비자의 정적 대조 기준으로 미구현 NID가 71개 남아 있다(이름은 #67에서 87/88 확인).

## 2. 방침

* [#67 설계](20261121-i067-boot-path-hle.md)의 방침을 그대로 쓴다: 측정이 이끄는 반복, 출처 있는 반환값만, 타이틀 중립, PPSSPP 코드 금지, 의미가 분명한 같은 API 묶음의 이웃 함수는 함께 구현 가능.
* `sceIoCloseAsync`는 기존 비동기 I/O(`sceIoOpenAsync`/`ReadAsync`/`LseekAsync`/`WaitAsync*`/`PollAsync`)와 같은 비동기 결과 모델로 구현한다. 같은 묶음의 미구현 함수(디렉터리 `sceIoDopen`/`Dread`/`Dclose`, `sceIoGetstat`, `sceIoMkdir`, `sceIoChdir` 등)는 불릴 때 구현한다.
* **멈춤 기준(완료 조건)**: 타이틀 화면이 표시되고 게임이 입력을 기다리는 상태(프레임이 계속 표시되고 새 HLE 정지가 없음)에 닿거나, 미구현 NID가 아닌 이유로 멈추면 끝낸다. 디스패치 미스는 먼저 lift 누락인지(코드가 조립하는 포인터) 확인해 근거와 함께 보고한다(lift 쪽은 소비자의 일이다).
* 입력: 소비자의 `--input-script`로 영상을 건너뛰고, 타이틀에서 필요한 버튼을 넣는다. 스크립트는 작업 로그에 남긴다.
* 저장 데이터·다이얼로그(`sceUtility` savedata/msgdialog)가 나오면 repsp의 기존 HLE와 소비자의 `--dialog-answer`로 진행한다. 새로 필요한 판단(예: 저장 데이터가 없을 때의 결과 코드)은 출처와 함께 정하고, 출처가 없으면 멈추고 묻는다.

## 3. 검증

1. 각 구현의 probe(CTest, `TIMEOUT`), 기존 probe 전부 통과.
2. Win64 Debug/Release 경고 0, Linux는 CI.
3. 소비자 측정 반복(멈춘 NID, 구현한 것)을 작업 로그에 시간순으로, 마지막 상태(타이틀 화면이면 그 프레임 번호와 화면 설명)와 정적 대조 수치 변화를 남긴다.

---

# Design: boot-path HLE from the opening movie to the title screen (issue #71)

Consumer need: [reexec/reDMP2#9](https://github.com/reexec/reDMP2/issues/9).

## 1. Starting point (confirmed)

* With repsp v0.0.32 the consumer's opening movie (PMF) decodes and plays (with the optional FFmpeg codec plugin).
* Pressing START through an input script to skip it stops at `IoFileMgrForUser` `sceIoCloseAsync` (`0xFF5940B6`), unimplemented.
* The consumer's static match still lists 71 unimplemented NIDs (names confirmed for 87 of 88 in #67).

## 2. Approach

* The approach of the [#67 design](20261121-i067-boot-path-hle.md) carries over:
  * a measured loop
  * sourced return values only
  * title-neutral code
  * no PPSSPP code
  * clear siblings in the same API family may be implemented together
* `sceIoCloseAsync` follows the existing asynchronous I/O result model (`sceIoOpenAsync`/`ReadAsync`/`LseekAsync`/`WaitAsync*`/`PollAsync`). Other unimplemented functions of that family (directories `sceIoDopen`/`Dread`/`Dclose`, `sceIoGetstat`, `sceIoMkdir`, `sceIoChdir`, ...) are implemented when called.
* **Stopping rule (done criterion)**: the issue ends when either happens:
  * the title screen is presented and the game waits for input (frames keep coming, no new HLE stop)
  * the run stops for a reason other than an unimplemented NID

  A dispatch miss is first checked for a lift miss (a code-built pointer) and reported with evidence; the lift side is the consumer's job.
* Input: the consumer's `--input-script` skips the movie and supplies the buttons the title needs; the script goes into the work log.
* Savedata and dialogs (`sceUtility` savedata/msgdialog) proceed through repsp's existing HLE and the consumer's `--dialog-answer`. New decisions (for example the result code when no savedata exists) are made with a source; without one, stop and ask.

## 3. Verification

1. A probe per implementation (CTest, `TIMEOUT`); all existing probes pass.
2. Win64 Debug/Release with zero warnings; Linux in CI.
3. The work log records, in order:
   * the consumer-side loop (the NID it stopped on, what was implemented)
   * the final state (for a title screen, its frame number and a description)
   * the change in static coverage numbers
