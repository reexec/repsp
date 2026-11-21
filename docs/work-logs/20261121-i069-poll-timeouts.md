# 작업 로그: `Scheduler::Poll()`에서 만료된 시간제한 대기 깨우기 (issue #69)

* 이슈: [#69](https://github.com/reexec/repsp/issues/69) (소비자 요구: [reexec/reDMP2#6](https://github.com/reexec/reDMP2/issues/6))
* 설계: [20261121-i069-poll-timeouts.md](../design/20261121-i069-poll-timeouts.md)
* 작업 지시: [20261121-i069-poll-timeouts.md](../work-orders/20261121-i069-poll-timeouts.md)
* 브랜치: `i69-poll-timeouts`

---

## 1. 변경 요약

1. **안전 지점 만료 처리 (`include/repsp/runtime/scheduler.h`, `src/runtime/scheduler.cpp`)**
   * `Scheduler::ExpireTimedWaitsLocked(std::chrono::steady_clock::time_point now)`를 private 헬퍼로 분리해, `Scheduler::Run()`의 유휴 경로와 `Scheduler::Poll()`이 동일한 만료 처리를 공유하도록 변경했습니다.
   * `timed_waiter_count_` 카운터를 추가해 `WaitCurrentIf()`에서 `has_timeout`을 설정할 때 증가시키고, `MakeReadyLocked()`, `TerminateThread()`에서 `has_timeout`을 해제할 때 감소시키며, `Reset()`에서 0으로 초기화합니다. 활성 시간제한 대기가 없으면 `Poll()`이 시계(`steady_clock::now()`)를 읽지 않습니다.
   * `Scheduler::Poll()` 순서를 `DeliverPendingInterrupts()` → (`timed_waiter_count_ > 0`이면) `ExpireTimedWaitsLocked(now)` → `YieldIfPreempted()`로 갱신했습니다.
   * 우선순위, FIFO, uITRON 4.0 선점 규칙(선점된 스레드의 레디 큐 선두 복귀) 및 `CpuSuspendIntr` 중 선점 억제 계약은 변경하지 않았습니다.
2. **합성 프로브 추가 (`tools/kernel_sched_probe/main.cpp`)**
   * Test #18을 추가해 낮은 우선순위 스레드(prio 32)가 안전 지점(`Poll()`)을 반복 호출하며 바쁘게 도는 상황에서 다음을 검증합니다.
     * 높은 우선순위 스레드(prio 18)가 `DelayThread(2 ms)` 후 만료 시점 이후 첫 `Poll()`에서 깨어나 선점 실행되는지 확인.
     * `CpuSuspendIntr` 동안 마감 시점이 지나도 선점되지 않으며, `CpuResumeIntr` 직후 첫 `Poll()`에서 즉시 선점되는지 확인.
     * `WaitSema`의 2 ms 시간제한 대기도 동일한 `Poll()` 만료 경로로 `SCE_KERNEL_ERROR_WAIT_TIMEOUT`을 받으며 깨어나는지 확인.
3. **문서 갱신 (`ARCHITECTURE.md`)**
   * `ARCHITECTURE.md` §2.8의 스케줄러 안전 지점(`Scheduler::Poll()`) 계약(한/영)에 시간제한 대기 만료 처리 순서와 `timed_waiter_count_` 빠른 경로, `CpuSuspendIntr` 중 선점 억제 동작을 반영했습니다.

---

## 2. 검증 결과

### Win64 Debug 단독 빌드 및 CTest
```powershell
cmake --build third_party/repsp/build/win64-debug --config Debug
ctest --test-dir third_party/repsp/build/win64-debug -C Debug --output-on-failure
```
* 빌드 경고: **0건**.
* CTest: **23/23 통과** (`kernel_sched_probe` Test #18 포함, 8.23초).

### Win64 Release 단독 빌드 및 CTest
```powershell
cmake --build third_party/repsp/build/win64-release --config Release
ctest --test-dir third_party/repsp/build/win64-release -C Release --output-on-failure
```
* 빌드 경고: **0건**.
* CTest: **23/23 통과** (`kernel_sched_probe` Test #18 포함, 4.14초).

### 소비자 로컬 확인
* 소비자 저장소에서 이 서브모듈 작업 트리를 그대로 사용해 Release 빌드 후 헤드리스 600 VBlank 실행으로 확인했습니다.
* 낮은 우선순위 스레드가 안전 지점을 호출하며 바쁘게 도는 동안에도 높은 우선순위 주기 스레드가 `DelayThread`에서 정상적으로 반복 기상해 오디오·SAS 루프를 계속 구동함을 확인했습니다(실행 중 새 미구현 NID 없음; 소비자 측 세부 수치와 추가 원인 분석은 소비자 이슈에서 추적합니다).

---

# Work Log: Wake Expired Timed Waits in `Scheduler::Poll()` (issue #69)

* Issue: [#69](https://github.com/reexec/repsp/issues/69) (consumer request: [reexec/reDMP2#6](https://github.com/reexec/reDMP2/issues/6))
* Design: [20261121-i069-poll-timeouts.md](../design/20261121-i069-poll-timeouts.md)
* Work order: [20261121-i069-poll-timeouts.md](../work-orders/20261121-i069-poll-timeouts.md)
* Branch: `i69-poll-timeouts`

---

## 1. Summary of Changes

1. **Safe-point timeout expiry (`include/repsp/runtime/scheduler.h`, `src/runtime/scheduler.cpp`)**
   * Extracted `Scheduler::ExpireTimedWaitsLocked(std::chrono::steady_clock::time_point now)` so the idle path in `Scheduler::Run()` and `Scheduler::Poll()` share the exact same expiry logic.
   * Added an exact `timed_waiter_count_` counter incremented in `WaitCurrentIf()` when `has_timeout` is set, decremented in `MakeReadyLocked()` and `TerminateThread()` whenever `has_timeout` is cleared, and reset to 0 in `Reset()`. When no timed waiters exist, `Poll()` skips reading `steady_clock::now()`.
   * Updated `Scheduler::Poll()` order to: `DeliverPendingInterrupts()` → (if `timed_waiter_count_ > 0`) `ExpireTimedWaitsLocked(now)` → `YieldIfPreempted()`.
   * Left priority, FIFO, uITRON 4.0 preemption (front-of-queue re-insertion), and `CpuSuspendIntr` preemption suppression untouched.
2. **Synthetic probe (`tools/kernel_sched_probe/main.cpp`)**
   * Added Test #18 with a low-priority thread (prio 32) busy-looping through `Poll()` and a high-priority thread (prio 18), verifying:
     * `DelayThread(2 ms)` wakes and preempts on the first `Poll()` after its deadline;
     * no preemption occurs while `CpuSuspendIntr` is held past the deadline, and preemption happens immediately on the first `Poll()` after `CpuResumeIntr`;
     * `WaitSema` with a 2 ms timeout also expires through `Poll()` and returns `SCE_KERNEL_ERROR_WAIT_TIMEOUT`.
3. **Documentation (`ARCHITECTURE.md`)**
   * Updated `ARCHITECTURE.md` §2.8 (Korean and English) to document `Scheduler::Poll()`'s safe-point order, `timed_waiter_count_` fast path, and interaction with `CpuSuspendIntr`.

---

## 2. Verification Results

* **Win64 Debug standalone**: **0 warnings**, **23/23 CTest passed** (including `kernel_sched_probe` Test #18, 8.23 s).
* **Win64 Release standalone**: **0 warnings**, **23/23 CTest passed** (including `kernel_sched_probe` Test #18, 4.14 s).
* **Consumer local check**: Rebuilt the consumer in Release against this submodule working tree and ran a 600-VBlank headless boot. Confirmed that the higher-priority periodic thread now wakes repeatedly from `DelayThread` and drives its audio/SAS loop throughout the run even while a lower-priority thread busy-loops at safe points (no new unimplemented NID reached; consumer-specific metrics and follow-up investigation belong in the consumer issue).
