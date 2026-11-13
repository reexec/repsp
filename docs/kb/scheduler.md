# PSP 커널 스케줄러 규칙

이 문서는 PSP Allegrex 커널의 단일 코어 스레드 스케줄링, 대기 상태, 콜백 실행 규약을 정리한다.

## 1. 스케줄링 모델

* **단일 코어 고정 우선순위 선점형** (**추정**, [uOFW threadman](https://github.com/uofw/uofw)):
  * 숫자가 작을수록 우선순위가 높다(예: `0x10`이 `0x20`보다 높음).
  * 동일 우선순위 스레드 사이는 선입선출(FIFO)로 전환된다.
  * `sceKernelStartThread`, `sceKernelSignalSema`, `sceKernelSetEventFlag`, `sceKernelWakeupThread` 등이 더 높은 우선순위 스레드를 준비 상태로 만들면 즉시 문맥 전환이 일어난다.
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

## 4. repsp 구현 상태

* `repsp::runtime::Scheduler`의 우선순위 선점, 동일 우선순위 FIFO, sema/event flag/delay/sleep/WaitThreadEnd, 콜백 실행, 교착(`RaiseStop`) 보고는 `kernel_sched_probe`의 가짜 어댑터로 **확인됨**.

---

# PSP Kernel Scheduler Rules (English)

This document summarizes single-core thread scheduling, wait states, and callback execution rules in the PSP Allegrex kernel.

* **Model**: Single-core fixed-priority preemptive scheduler (**inferred**, [uOFW threadman](https://github.com/uofw/uofw)); lower numerical priority value means higher priority, and equal priorities are FIFO.
* **Stack & Arguments**: Thread stacks are allocated high in user partition 2 (256-byte aligned). `$gp` is inherited at creation time. `StartThread(arglen, argp)` copies `arglen` bytes to the top of the stack and sets `$a0 = arglen`, `$a1 = copied_addr` (**inferred**, uOFW).
* **Wait Codes & Callbacks**: Wait results (`0`, timeout `0x800201A8`, cancel `0x800201A9`, release `0x800201AA`, delete `0x800201B5`) and callback deletion on non-zero return are **inferred** from uOFW.
* **Verification**: Verified with synthetic tests in `kernel_sched_probe` (**confirmed** for synthetic probe behavior).
