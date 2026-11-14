# PSP 커널 스케줄러 규칙

이 문서는 PSP Allegrex 커널의 단일 코어 스레드 스케줄링, 대기 상태, 콜백 실행 규약을 정리한다.

## 1. 스케줄링 모델

* **단일 코어 고정 우선순위 선점형** (**추정**, [uOFW threadman](https://github.com/uofw/uofw), [uITRON 4.0 규격](https://www.tron.org/)):
  * 숫자가 작을수록 우선순위가 높다(예: `0x10`이 `0x20`보다 높음).
  * 동일 우선순위 스레드 사이는 선입선출(FIFO)로 전환된다.
  * **선점 vs 자발적 양보** (**추정**, uITRON 4.0 §3.2 / `rot_rdq`): 더 높은 우선순위 스레드에게 선점당한 실행 스레드는 **같은 우선순위 준비 큐의 맨 앞(head)**에 남는다. 반면 자발적 양보(`RotateThreadReadyQueue` / `YieldCurrent`)나 대기 상태에서 깨어난 스레드는 **맨 뒤(tail)**에 들어간다.
  * `sceKernelStartThread`, `sceKernelSignalSema`, `sceKernelSetEventFlag`, `sceKernelWakeupThread` 등이 더 높은 우선순위 스레드를 준비 상태로 만들면 즉시 문맥 전환이 일어난다.
* **동기화 객체 시그널 시점 인계 및 대기 큐 속성** (**추정**, [PSPSDK pspthreadman.h](https://github.com/pspdev/pspsdk), [uOFW threadman](https://github.com/uofw/uofw)):
  * `sceKernelSignalSema`와 `sceKernelSetEventFlag`는 대기자를 단순히 깨우는 것이 아니라, **signal/set 시점에 대기 큐 순서대로 자원(sema count 차감, event flag 출력 비트 기록 및 clear 옵션 적용)을 직접 인계**한다.
  * 속성 `0x100`(`PSP_SEMA_ATTR_PRIO` / `PSP_EVENT_WAITPRIO`): 대기 큐를 우선순위순(동일 우선순위는 FIFO)으로 정렬한다. 미설정(`0x000`)이면 FIFO다.
  * 속성 `0x200`(`PSP_EVENT_WAITMULTIPLE`): 이벤트 플래그에서 다중 대기자를 허용한다. 미설정 시 두 번째 대기자는 대기에 들어가지 않고 오류를 반환한다.
* **인터럽트 금지 (`CpuSuspendIntr` / `CpuResumeIntr`)** (**추정**, uOFW):
  * 인터럽트가 비활성화된 동안에는 안전점(`repsp_recomp_poll`) 선점과 콜백 전달이 지연된다.

## 2. 스레드 생성 및 `StartThread` 인자 전달

* **스택 할당**: 사용자 파티션(id 2)의 높은 쪽(`PSP_SMEM_High`)에서 256바이트 단위로 올림하여 할당한다 (**추정**, uOFW).
* **`$gp` 상속**: 스레드 생성 시점의 현재 `$gp` 레지스터 값을 새 스레드가 물려받는다 (**추정**, uOFW).
* **`StartThread(thid, arglen, argp)`**: `arglen > 0`이고 `argp != 0`이면 인자 블록을 게스트 스택 상단(16바이트 정렬)에 복사하고 `$a0 = arglen`, `$a1 = 복사본 주소`로 설정하며, `$sp`는 그 아래 16바이트 정렬 위치로 둔다 (**추정**, uOFW).
* **스레드 종료**: 진입 함수가 반환하면 `$v0`을 종료 상태로 간주하여 `ExitThread`와 동일하게 처리하며, `WaitThreadEnd`로 대기 중인 스레드들을 깨운다.

## 3. 대기 결과 코드 및 콜백

* 주요 대기 결과 코드 (**추정**, uOFW):
  * `0`: 정상 완료
  * `0x800201A8` (`SCE_KERNEL_ERROR_WAIT_TIMEOUT`): 시간 초과
  * `0x800201A9` (`SCE_KERNEL_ERROR_WAIT_CANCEL`): 대기 취소
  * `0x800201AA` (`SCE_KERNEL_ERROR_RELEASE_WAIT`): 강제 대기 해제
  * `0x800201B5` (`SCE_KERNEL_ERROR_WAIT_DELETE`): 대기 대상 커널 객체 삭제
* **콜백 (`...CB` 대기, `sceKernelCheckCallback`)** (**추정**, uOFW):
  * 스레드에 연결된 콜백이 `notify_count > 0`이면 해당 스레드의 컨텍스트에서 호출된다.
  * 콜백이 `0`이 아닌 값을 반환하면 콜백 객체가 삭제된다.

## 4. 인터럽트 전달 규칙 (**추정**: uOFW; **확인됨**: `kernel_sched_probe`)

* **전달 시점**: `Scheduler::QueueInterrupt`로 적재된 인터럽트 핸들러는 인터럽트가 활성화(`CpuResumeIntr` 상태)된 다음 안전점(`Poll` / `YieldCurrent`)에서 전달됩니다.
* **전용 인터럽트 스택**: 핸들러는 현재 스레드의 사용자 스택이 아니라 스케줄러가 커널/사용자 영역에서 확보한 전용 16 KiB 인터럽트 스택 위에서 실행되며, 완료 후 원래 컨텍스트로 복귀합니다.
* **핸들러 안 대기 금지**: 인터럽트 핸들러 실행 중(`InInterruptContext()`)에는 `WaitCurrent` 등 스레드를 대기 상태로 전환하는 커널 호출을 할 수 없으며, 호출 시 정지 보고(`RaiseStop`)합니다.

## 5. repsp 구현 상태

* `repsp::runtime::Scheduler`의 우선순위 선점, 동일 우선순위 FIFO, sema/event flag/delay/sleep/WaitThreadEnd, 콜백 실행, 인터럽트 스택 전달 및 핸들러 내 대기 금지, 교착(`RaiseStop`) 보고는 `kernel_sched_probe`의 가짜 어댑터로 **확인됨**.

---

# PSP Kernel Scheduler Rules (English)

This document summarizes single-core thread scheduling, wait states, and callback execution rules in the PSP Allegrex kernel.

* **Model**: Single-core fixed-priority preemptive scheduler (**inferred**, [uOFW threadman](https://github.com/uofw/uofw), uITRON 4.0); lower numerical priority value means higher priority, and equal priorities are FIFO. A preempted thread stays at the **front** of its priority's ready queue, whereas voluntary yields (`RotateThreadReadyQueue`/`YieldCurrent`) and threads waking from wait go to the **back**.
* **Sync Object Handoff & Queue Attributes**: `sceKernelSignalSema` and `sceKernelSetEventFlag` hand off resources directly at signal/set time in wait-queue order (attribute `0x100` selects priority order instead of FIFO; attribute `0x200` `PSP_EVENT_WAITMULTIPLE` is required for multiple concurrent event-flag waiters) (**inferred**, PSPSDK `pspthreadman.h`, uOFW).
* **Stack & Arguments**: Thread stacks are allocated high in user partition 2 (256-byte aligned). `$gp` is inherited at creation time. `StartThread(arglen, argp)` copies `arglen` bytes to the top of the stack and sets `$a0 = arglen`, `$a1 = copied_addr` (**inferred**, uOFW).
* **Wait Codes & Callbacks**: Wait results (`0`, timeout `0x800201A8`, cancel `0x800201A9`, release `0x800201AA`, delete `0x800201B5`) and callback deletion on non-zero return are **inferred** from uOFW.
* **Interrupt Delivery Rules** (**inferred**, uOFW; **confirmed**, `kernel_sched_probe`): Queued interrupts are delivered at the next safe point (`Poll`/`YieldCurrent`) while interrupts are enabled, run on a dedicated 16 KiB interrupt stack rather than the interrupted thread's stack, and may not block (`WaitCurrent` inside an interrupt handler triggers `RaiseStop`).
* **Verification**: Verified with synthetic tests in `kernel_sched_probe` (**confirmed** for synthetic probe behavior).
