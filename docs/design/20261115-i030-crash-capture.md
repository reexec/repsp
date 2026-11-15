# 설계: 크래시 기록 — 강제 종료를 분석할 수 있게 남기기 (issue #30)

## 1. 요구사항

* 소비자([reDMP1 issue #31](https://github.com/reexec/reDMP1/issues/31))에서 사용자가 창 모드로 플레이하다 프로그램이 강제 종료됐다. Windows가 남긴 덤프로는 C 런타임의 `abort()`(fast-fail `0xC0000409`, 인자 7)까지만 알 수 있었다: 정지 보고는 창 모드에서 보이지 않고, Release 빌드에는 심볼이 없고, 게스트가 어디를 실행하고 있었는지와 어떤 입력이 들어갔는지가 남지 않는다.
* 다음에 같은 일이 생기면 **그 자리에서 원인을 좁힐 수 있는 기록**과 **같은 상황을 다시 만드는 수단**이 남아야 한다.
* 정상 실행의 비용은 무시할 만해야 한다. 타이틀에 묶인 코드는 넣지 않는다.

## 2. 크래시 처리기(`repsp_platform`)

* 처리하는 종료: 처리되지 않은 네이티브 예외(접근 위반 등; 지금 `GuardedRun`이 잡지 않는 스레드 포함), `abort()`와 `std::terminate`(처리되지 않은 C++ 예외, 합류하지 않은 `std::thread` 파괴 등), Linux의 치명 신호(`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`).
* 처리기는 한 번만 실행되고, 할 수 있는 만큼 기록한 뒤 원래의 종료로 넘긴다(종료 코드는 그대로). 처리기 안에서는 할당과 잠금을 피한다(미리 잡아 둔 버퍼, 이미 연 파일).
* 남기는 것(소비자가 정한 크래시 디렉터리에, 실행마다 시각이 붙은 이름으로):
  * **Windows 미니덤프**(`MiniDumpWriteDump`): 스레드, 스택, 모듈, 그리고 게스트 메모리 영역을 뺀 데이터. 크기를 수십 MB 안으로.
  * **크래시 보고**(텍스트): 종료 종류(예외 코드와 주소, 신호, `std::terminate`라면 처리되지 않은 예외의 `what()`), 크래시가 난 호스트 스레드와 그 스레드가 실행하던 게스트 스레드(이름, UID, 우선순위), 게스트 레지스터, **게스트 최근 실행 기록**(아래), 최근 HLE 호출, 스케줄러의 스레드 목록과 대기 상태, 그 시점의 vblank 수, 앱이 등록한 추가 정보(빌드 버전, 렌더러, 옵션).
* **게스트 최근 실행 기록**: 게스트 스레드마다 최근 HLE 호출 몇 개(NID, 인자, 반환값)와, 소비자가 제공하는 "최근 게스트 함수 진입" 기록(아래 소비자 계약)을 고리 버퍼로 둔다. 정상 실행에서도 켜져 있을 만큼 싸야 한다(고정 크기 배열에 쓰기만).
* 처리기에 소비자가 정보를 덧붙이는 콜백(`SetCrashReportExtra`)을 둔다. 콜백이 실패해도 나머지 기록은 남는다.

## 3. 로그와 입력 기록

* **실행 로그 파일**: 지금 stdout/stderr로만 가는 로그와 정지 보고를 파일에도 쓴다(소비자가 경로를 준다). 창 모드에서 콘솔이 없어도 남는다. 크기 상한을 두고 오래된 실행의 로그는 정해진 개수만 남긴다.
* **입력 기록**(`repsp_hle_ctrl`): vblank마다의 버튼 상태가 바뀔 때만 `<vblank> <버튼 이름들>`을 기록한다. 형식은 소비자의 `--input-script`와 같아서 그대로 재생할 수 있다. 크래시 처리기는 기록을 마저 써서 닫는다.
* 재생의 한계를 문서에 적는다: 오디오와 vblank가 호스트 시각을 따르므로 재생이 같은 순간을 정확히 되풀이하지는 않는다. 같은 입력으로 같은 화면 흐름은 다시 만들 수 있다.

## 4. 소비자 계약

* `platform::InstallCrashHandler(CrashConfig)`: 크래시 디렉터리, 미니덤프 사용 여부, 추가 정보 콜백.
* `platform::SetLogFile(path, limits)`.
* `hle::ctrl::StartInputRecording(path)` / `StopInputRecording()`.
* 게스트 함수 진입 기록을 소비자가 넣는 함수(`runtime::NoteGuestEntry(addr)`): 생성 코드의 계측 지점에서 부르게 하는 것은 소비자의 선택이다.

## 5. 이번 task의 검증

1. `crash_probe`: 자식 프로세스로 각 종료 종류(접근 위반, `abort`, 처리되지 않은 C++ 예외, 합류하지 않은 스레드, Linux 신호)를 일으키고, 크래시 보고와(Windows에서는) 미니덤프가 생기며 보고에 종료 종류와 게스트 스레드 정보가 있는 것을 확인한다. 종료 코드가 원래대로인 것도 확인한다.
2. 입력 기록 probe: 합성 입력을 기록한 파일이 `--input-script` 형식과 같고, 다시 읽었을 때 같은 상태열이 나온다.
3. 기존 probe가 그대로 통과한다. 정상 실행의 비용이 측정상 무시할 만하다(작업 로그에 수치).
4. Win64 Debug/Release, Linux x64 Debug/Release 빌드와 CTest.

---

# Design: crash capture — leaving enough behind to analyze a forced termination (issue #30)

## 1. Requirements

In the consumer ([reDMP1 issue #31](https://github.com/reexec/reDMP1/issues/31)) the program terminated abruptly while the user was playing in windowed mode. The dump Windows left only shows a C runtime `abort()` (fast-fail `0xC0000409`, parameter 7): the stop report is not visible in windowed mode, the Release build has no symbols, and nothing records where the guest was executing or what input it had received. Next time, **records that narrow the cause on the spot** and **a means to recreate the situation** must be left behind. The cost in normal runs must be negligible, and no title-specific code goes in.

## 2. Crash handler (`repsp_platform`)

Terminations handled: unhandled native exceptions (access violations and the like, including on threads `GuardedRun` does not cover), `abort()` and `std::terminate` (an unhandled C++ exception, destroying a joinable `std::thread`, and so on), and on Linux the fatal signals (`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`). The handler runs once, records what it can and then passes on to the original termination (the exit code is unchanged); it avoids allocation and locks (pre-allocated buffers, files opened in advance). What it leaves, in a crash directory chosen by the consumer under a time-stamped name per run: a **Windows minidump** (`MiniDumpWriteDump`: threads, stacks, modules and data excluding the guest memory region, kept to tens of MB); a **crash report** (text): the kind of termination (exception code and address, signal, or for `std::terminate` the unhandled exception's `what()`), the crashing host thread and the guest thread it was running (name, UID, priority), guest registers, the **recent guest execution record** (below), recent HLE calls, the scheduler's thread list and wait states, the vblank count, and extra information registered by the app (build version, renderer, options). **Recent guest execution record**: per guest thread, a ring buffer of the last few HLE calls (NID, arguments, return value) and the "recent guest function entries" the consumer supplies (consumer contract below), cheap enough to stay on in normal runs (writes into fixed-size arrays only). A callback for the consumer to add information (`SetCrashReportExtra`); if it fails, the rest is still recorded.

## 3. Logs and input recording

**Run log file**: the log and stop report, now only on stdout/stderr, also go to a file (path from the consumer), so they survive in windowed mode without a console; with a size limit and only a set number of older runs kept. **Input recording** (`repsp_hle_ctrl`): whenever the button state changes at a vblank, `<vblank> <button names>` is written — the same format as the consumer's `--input-script`, so it replays as is; the crash handler flushes and closes it. The limit of replay is documented: audio and vblanks follow host time, so a replay does not repeat the same instant exactly, but the same input recreates the same flow of screens.

## 4. Consumer contract

`platform::InstallCrashHandler(CrashConfig)` (crash directory, whether to write a minidump, the extra information callback); `platform::SetLogFile(path, limits)`; `hle::ctrl::StartInputRecording(path)` / `StopInputRecording()`; a function through which the consumer records guest function entries (`runtime::NoteGuestEntry(addr)`) — whether the generated code's instrumentation points call it is the consumer's choice.

## 5. Verification for this task

1. `crash_probe`: child processes raise each kind of termination (access violation, `abort`, an unhandled C++ exception, a joinable thread destroyed, Linux signals), and a crash report and (on Windows) a minidump appear, the report naming the kind of termination and the guest thread; the exit code is unchanged.
2. Input recording probe: a file recorded from synthetic input has the `--input-script` format and reads back to the same sequence of states.
3. Existing probes pass unchanged, and the cost in normal runs is negligible by measurement (figures in the work log).
4. Win64 Debug/Release and Linux x64 Debug/Release builds and CTest.
