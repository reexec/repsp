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
| `SCE_KERNEL_ERROR_PRX_TYPE_UNSUPPORTED` (`kSceKernelErrorPrxTypeUnsupported`) | `0x80020148` | uOFW `modulemgr` | **추정** |
| `SCE_KERNEL_ERROR_BADF` | `0x80020323` | PSPSDK `pspiofilemgr_kernel.h` / uOFW | **추정** |
| `SCE_KERNEL_ERROR_ASYNC_BUSY` | `0x80020329` | uOFW | **추정** |
| `SCE_AUDIO_ERROR_NOT_INITIALIZED` | `0x80260001` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_OUTPUT_BUSY` | `0x80260002` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_INVALID_CH` | `0x80260003` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_NOT_FOUND` | `0x80260005` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_INVALID_SIZE` | `0x80260006` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_INVALID_FORMAT` | `0x80260007` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_NOT_RESERVED` | `0x80260008` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_AUDIO_ERROR_INVALID_VOLUME` | `0x8026000B` | PSPSDK `pspaudio.h` / uOFW | **추정** |
| `SCE_SAS_ERROR_INVALID_GRAIN` | `0x80420001` | 공개 SAS 규격 / PSDevWiki | **추정** |
| `SCE_SAS_ERROR_INVALID_MAX_VOICES` | `0x80420002` | 공개 SAS 규격 / PSDevWiki | **추정** |
| `SCE_SAS_ERROR_INVALID_VOICE` | `0x80420010` | 공개 SAS 규격 / PSDevWiki | **추정** |
| `SCE_SAS_ERROR_INVALID_PARAMETER` | `0x80420014` | 공개 SAS 규격 / PSDevWiki | **추정** |
| `SCE_SAS_ERROR_INVALID_FX_TYPE` | `0x80420020` | 공개 SAS 규격 / PSDevWiki | **추정** |
| `SCE_ATRAC_ERROR_PARAM_FAIL` | `0x80630001` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_API_FAIL` | `0x80630002` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_NO_ATRACID` | `0x80630003` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_BAD_CODEC_TYPE` | `0x80630004` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_BAD_ATRACID` | `0x80630005` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_UNKNOWN_FORMAT` | `0x80630006` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_UNMATCH_FORMAT` | `0x80630007` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_BAD_DATA` | `0x80630008` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_ALLDATA_IS_ON_MEMORY` | `0x80630009` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_UNSET_DATA` | `0x80630010` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_READSIZE_IS_TOO_SMALL` | `0x80630011` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_NOT_4BYTE_ALIGNMENT` | `0x80630014` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_BAD_SAMPLE` | `0x80630015` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_ADD_DATA_IS_TOO_BIG` | `0x80630018` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ATRAC_ERROR_ALLDATA_WAS_DECODED` | `0x80630024` | PSPSDK `pspatrac3.h` | **추정** |
| `SCE_ERROR_ERRNO_ENOENT` | `0x80010002` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_ERRNO_EACCES` | `0x8001000D` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_ERRNO_EEXIST` | `0x80010011` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_ERRNO_EINVAL` | `0x80010016` | PSPSDK `errno.h` | **추정** |
| `SCE_ERROR_INVALID_POINTER` | `0x80000103` | uOFW `include/common/errors.h` | **추정** |
| `SCE_ERROR_INVALID_SIZE` | `0x80000104` | uOFW `include/common/errors.h` | **추정** |
| `SCE_ERROR_INVALID_MODE` | `0x80000107` | uOFW `include/common/errors.h` | **추정** |
| `SCE_ERROR_INVALID_VALUE` | `0x800001FE` | uOFW `include/common/errors.h` | **추정** |

---

# PSP Kernel Error Codes (English)

This document lists the kernel and I/O error codes defined in `include/repsp/hle/kernel_errors.h` along with their sources. Unconfirmed error cases fall back to `SCE_KERNEL_ERROR_ERROR` (`0x80020001`). All values above are **inferred** from [uOFW](https://github.com/uofw/uofw) and PSPSDK public headers until verified against hardware or title behavior.
