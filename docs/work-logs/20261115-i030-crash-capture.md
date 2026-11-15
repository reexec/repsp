# 작업 로그: 크래시 포착, 실행 이력, 입력 녹화 및 `audio_probe` 교착 수정 (issue #30·#31)

## 1. 작업 개요

* 대상 저장소/브랜치: `third_party/repsp` (`i030-crash-capture`, `main` `1a69cee` 위 리베이스 상태)
* 목표: 비정상 종료 포착(`crash_handler`), 스레드별 최근 게스트 진입·HLE 호출 링 버퍼(`exec_history`), `TrackedMutex` 소유자 추적, 실행 로그(`SetLogFile`), 입력 녹화(`StartInputRecording`/`StopInputRecording`), `audio_probe` 간헐적 정지 원인 진단·수정, CTest `TIMEOUT` 추가.

## 2. 구현 내용

1. **`TrackedMutex` 및 스레드 실행 이력 완성 (`scheduler.cpp`, `exec_history.cpp`)**:
   * `exec_history.cpp`의 `GetVblankClock().Count()` 호출을 정정했다.
   * `scheduler.cpp`의 남은 `std::lock_guard<std::mutex>`를 모두 `std::lock_guard<TrackedMutex>`로 전환하고 `TrackedMutex::lock`/`unlock`/`try_lock`에서 `SetCrashSchedulerMutexOwner`를 갱신하도록 완성했다.
   * `Scheduler::ThreadEntry`에서 `SetCurrentThreadExecInfo`와 종료 시 `ClearCurrentThreadExecInfo`를 호출해 크래시 보고서의 `crashing_guest_thread`에 게스트 스레드 UID·이름·우선순위가 기록되도록 했다.
2. **입력 녹화 (`include/repsp/hle/ctrl.h`, `src/hle/ctrl/hle_ctrl.cpp`, `tools/ctrl_probe/main.cpp`)**:
   * `StartInputRecording(path)` / `StopInputRecording()`을 구현했다. `# repsp input recording (<vblank> <buttons>)` 헤더를 쓰고 버튼 상태 변화 시마다 `<vblank> <button names>`를 기록·플러시하며, `RegisterCrashCleanupCallback`으로 비정상 종료 시 안전하게 닫는다.
   * `ctrl_probe`에 7번째 검사(`LoadInputScript` 왕복 검증)를 추가했다.
3. **크래시 프로브 (`tools/crash_probe/main.cpp`, `tools/CMakeLists.txt`)**:
   * `access_violation`, `guest_abort`, `abort`, `cpp_exception` (`noexcept` 경계), `unjoined_thread`, `mutex_held_stop` 6가지 종료 경로를 검증한다.
   * Windows CTest에서 자식 프로세스 종료 후 WER 파이프 핸들 상속으로 `crash_probe`가 30초 타임아웃에 걸리던 문제를 `STARTF_USESTDHANDLES` + `CREATE_NO_WINDOW` 및 `WerSetFlags(0x24)`로 해결하고 6개 자식을 병렬 실행해 CTest 30초 제한 안에 통과시켰다.
   * `add_repsp_probe`에 `TIMEOUT 30`을 설정했다.
4. **`audio_probe` Release 간헐적 정지 진단 및 수정 (`scheduler.h`, `scheduler.cpp`, `hle_audio.cpp`)**:
   * **재현**: Release(`repsp-win64-i030-rel`)에서 100회 중 98번째에 타임아웃으로 재현했고, 병렬 스트레스 실행에서 `[INFO] [sceAudio] Registered 6 sceAudio NIDs` 직후 정지함을 확인했다.
   * **원인 확정**: `OutputChannelCommon`이 `g_audio_mutex`를 잠금 해제한 뒤 `sched.WaitCurrent(WaitType::kAudio, ...)`에 진입하기 직전 창에서 `ClockThreadMain`이 버퍼를 비우고 `sched.WakeAllWaiters(WaitType::kAudio, ...)`를 먼저 호출하면 깨우기 신호가 유실(lost wakeup)되어 영구 대기에 빠졌다.
   * **수정**: `Scheduler::WaitCurrentIf(..., should_wait)`를 추가해 `Scheduler::mutex_`를 잡은 상태에서 조건을 다시 검사하고, `hle_audio.cpp`가 `g_audio_mutex` 아래서 조건을 재확인하도록 수정했다(`Scheduler::mutex_` → `g_audio_mutex` 단방향 잠금 순서 유지).

## 3. 설계 질문: 뮤텍스 보유 중 정지(`RaiseStop`)·예외 경로 정리 방향

* **확인된 위험 경로**:
  * `Scheduler::Run` (`scheduler.cpp`)에서 알 수 없는 `entry_thread_uid`일 때 `std::lock_guard<TrackedMutex> lock(mutex_)`를 쥔 채 `platform::RaiseStop(...)`을 호출한다.
  * `/EHsc` 아래서는 `GuardedRun` 내부의 `RaiseStop` 또는 하드웨어 예외(`EXCEPTION_STACK_OVERFLOW` 등)가 C++ 소멸자를 실행하지 않으므로, 바깥 `ThreadEntry`가 `mutex_`를 다시 잠글 때 `EDEADLK` → `std::terminate()` → `abort()`로 이어진다(`crash_probe`의 `mutex_held_stop`으로 재현 확인).
* **선택지**:
  1. **(권장)** `Scheduler::Run` 등 `RaiseStop`을 부르는 모든 호스트 프레임에서 호출 직전에 명시적으로 `lock.unlock()`하고, `GuardedRun` 경계 안팎에서 뮤텍스를 쥔 채 게스트/정지 경로로 들어가지 않는 규약을 강제한다(`TrackedMutex` 소유자 정보로 위반 시 즉시 진단).
  2. `GuardedRun` 포착 시 해당 스레드가 쥔 `TrackedMutex`를 강제 해제하거나 `/EHa`(비동기 SEH 소멸자 실행)로 전환한다. 단, `/EHa`는 재컴파일 C 코드와 전체 빌드 크기·성능에 영향을 주며 깨진 불변식을 숨길 위험이 있다.

## 4. 검증 결과

* **Release `audio_probe` 병렬 스트레스(400회)**: `400/400 passed`.
* **Win64 Debug CTest**: `22/22` 통과 (`19.91 sec`, `crash_probe` `15.12 sec`).
* **Win64 Release CTest**: `22/22` 통과 (`18.21 sec`, `crash_probe` `15.11 sec`).
* 참고: 샌드박스 제약으로 `git -C third_party\repsp` 명령이 차단되어 서브모듈 변경 사항은 작업 트리에 둔 상태로 마쳤다.

---

# Work Log: Crash Capture, Execution History, Input Recording, and `audio_probe` Hang Fix (issue #30/#31)

## 1. Overview

* Target repo/branch: `third_party/repsp` (`i030-crash-capture`, rebased onto `main` `1a69cee`)
* Goal: Complete crash capture (`crash_handler`), per-thread guest entry / HLE call ring buffers (`exec_history`), `TrackedMutex` owner tracking, run log (`SetLogFile`), input recording (`StartInputRecording`/`StopInputRecording`), diagnose and fix intermittent `audio_probe` hang, and add CTest `TIMEOUT`.

## 2. Changes

1. **`TrackedMutex` & Thread Execution History (`scheduler.cpp`, `exec_history.cpp`)**:
   * Fixed `GetVblankClock().Count()` in `exec_history.cpp`.
   * Converted all remaining `std::lock_guard<std::mutex>` in `scheduler.cpp` to `std::lock_guard<TrackedMutex>` and wired `TrackedMutex` to update `SetCrashSchedulerMutexOwner`.
   * Updated `Scheduler::ThreadEntry` to call `SetCurrentThreadExecInfo` / `ClearCurrentThreadExecInfo` so `crashing_guest_thread` records guest UID, name, and priority.
2. **Input Recording (`ctrl.h`, `hle_ctrl.cpp`, `ctrl_probe/main.cpp`)**:
   * Implemented `StartInputRecording`/`StopInputRecording`, writing `# repsp input recording (<vblank> <buttons>)` and flushed button-change lines compatible with `LoadInputScript`, with crash-cleanup closing. Added check 7 to `ctrl_probe`.
3. **Crash Probe & CTest Timeouts (`crash_probe/main.cpp`, `tools/CMakeLists.txt`)**:
   * Verified 6 termination kinds (`access_violation`, `guest_abort`, `abort`, `cpp_exception`, `unjoined_thread`, `mutex_held_stop`).
   * Fixed CTest pipe handle inheritance hang on Windows via `STARTF_USESTDHANDLES` + `CREATE_NO_WINDOW`, `WerSetFlags(0x24)`, and parallel child execution. Added `TIMEOUT 30` to `add_repsp_probe`.
4. **`audio_probe` Release Intermittent Hang (`scheduler.h`, `scheduler.cpp`, `hle_audio.cpp`)**:
   * **Reproduction**: Timed out at iteration 98/100 in Release and during parallel stress runs right after `[INFO] [sceAudio] Registered 6 sceAudio NIDs`.
   * **Root Cause**: Lost wakeup window in `OutputChannelCommon` between unlocking `g_audio_mutex` and calling `sched.WaitCurrent(WaitType::kAudio, ...)`, during which `ClockThreadMain` drained the buffer and called `WakeAllWaiters`.
   * **Fix**: Added `Scheduler::WaitCurrentIf` so `hle_audio` re-checks the wait predicate under `g_audio_mutex` while `Scheduler::mutex_` is held (preserving strict `Scheduler::mutex_` → `g_audio_mutex` lock order).

## 3. Design Question: Fixing Mutex-Held `RaiseStop` / Fault Paths

* **Observed hazard**: `Scheduler::Run` calls `platform::RaiseStop` while holding `mutex_`. Under `/EHsc`, SEH unwinding out of `GuardedRun` skips C++ destructors, causing `EDEADLK` → `std::terminate()` → `abort()` on outer re-lock (reproduced by `crash_probe`'s `mutex_held_stop`).
* **Options**: (1, Recommended) Explicitly unlock `mutex_` before calling `RaiseStop` (e.g. `Scheduler::Run`) and enforce the lock-free `GuardedRun`/`RaiseStop` boundary contract; or (2) switch to `/EHa` or force-unlock tracked mutexes on unwind (risks masking broken invariants and impacting generated code builds).

## 4. Verification

* **Release `audio_probe` 400-run parallel stress test**: `400/400 passed`.
* **Win64 Debug CTest**: `22/22` passed (`19.91 sec`).
* **Win64 Release CTest**: `22/22` passed (`18.21 sec`).
* Note: `git -C third_party\repsp` is blocked in the sandbox, so submodule changes remain in the working tree.
