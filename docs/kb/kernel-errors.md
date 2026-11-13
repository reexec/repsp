# PSP 커널 오류 코드

이 문서는 `include/repsp/hle/kernel_errors.h`에 정의된 커널·파일 입출력 오류 코드와 그 출처를 정리한다.

* **원칙**: 출처가 확인되지 않은 오류 코드는 임의로 지어내지 않으며, 미확정 오류는 `SCE_KERNEL_ERROR_ERROR` (`0x80020001`)로 처리한다.

## 오류 코드 표

| 기호 | 값 | 출처 | 상태 |
|---|---|---|---|
| `SCE_KERNEL_ERROR_ERROR` | `0x80020001` | [uOFW errors.h](https://github.com/uofw/uofw) | **추정** |
| `SCE_KERNEL_ERROR_NOTIMP` | `0x80020002` | uOFW / PSPSDK | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_PERM` | `0x8002000B` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_CONTEXT` | `0x80020064` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_UNKNOWN_MBID` | `0x800200CF` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_ADDR` | `0x800200D6` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE` | `0x800200D7` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE` | `0x800200D8` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_NO_MEMORY` | `0x800200D9` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_PARTITION_ID` | `0x800200DC` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_UNKNOWN_THID` | `0x80020198` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_UNKNOWN_SEMID` | `0x80020199` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_UNKNOWN_EVFID` | `0x8002019A` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_UNKNOWN_CBID` | `0x8002019D` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_NOT_DORMANT` | `0x8002019F` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_DORMANT` | `0x800201A0` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_PRIORITY` | `0x800201A3` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_ILLEGAL_STACK_SIZE` | `0x800201A4` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_SEMA_ZERO` | `0x800201A6` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_EVF_COND` | `0x800201A7` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_WAIT_TIMEOUT` | `0x800201A8` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_WAIT_CANCEL` | `0x800201A9` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_RELEASE_WAIT` | `0x800201AA` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_WAIT_DELETE` | `0x800201B5` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_SEMA_OVF` | `0x800201B8` | uOFW | **추정** |
| `SCE_KERNEL_ERROR_BADF` | `0x80020323` | PSPSDK `pspiofilemgr_kernel.h` / uOFW | **추정** |
| `SCE_KERNEL_ERROR_ASYNC_BUSY` | `0x80020329` | uOFW | **추정** |
| `SCE_ERROR_ERRNO_ENOENT` | `0x80010002` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_ERRNO_EACCES` | `0x8001000D` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_ERRNO_EEXIST` | `0x80010011` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_ERRNO_EINVAL` | `0x80010016` | PSPSDK `errno.h` | **추정** |

---

# PSP Kernel Error Codes (English)

This document lists the kernel and I/O error codes defined in `include/repsp/hle/kernel_errors.h` along with their sources. Unconfirmed error cases fall back to `SCE_KERNEL_ERROR_ERROR` (`0x80020001`). All values above are **inferred** from [uOFW](https://github.com/uofw/uofw) and PSPSDK public headers until verified against hardware or title behavior.
