#ifndef REPSP_HLE_KERNEL_ERRORS_H
#define REPSP_HLE_KERNEL_ERRORS_H

#include <cstdint>

namespace repsp::hle
{

// Standard PSP kernel and errno codes.
// Sources: uOFW (MIT, include/common/errors.h) and PSPSDK (BSD, pspkerneltypes.h / pspiofilemgr_kernel.h).
// Any case without a confirmed specific error code falls back to kSceKernelErrorError (0x80020001).

constexpr int32_t kSceKernelErrorOk = 0;

// Generic / Uncategorized (uOFW)
constexpr int32_t kSceKernelErrorError = static_cast<int32_t>(0x80020001u);            // SCE_KERNEL_ERROR_ERROR
constexpr int32_t kSceKernelErrorNotImp = static_cast<int32_t>(0x80020002u);           // SCE_KERNEL_ERROR_NOTIMP
constexpr int32_t kSceKernelErrorIllegalPerm = static_cast<int32_t>(0x8002000Bu);      // SCE_KERNEL_ERROR_ILLEGAL_PERM
constexpr int32_t kSceKernelErrorIllegalContext = static_cast<int32_t>(0x80020064u);   // SCE_KERNEL_ERROR_ILLEGAL_CONTEXT

// SysMem (uOFW)
constexpr int32_t kSceKernelErrorUnknownMbid = static_cast<int32_t>(0x800200CFu);      // SCE_KERNEL_ERROR_UNKNOWN_MBID
constexpr int32_t kSceKernelErrorIllegalAddr = static_cast<int32_t>(0x800200D6u);      // SCE_KERNEL_ERROR_ILLEGAL_ADDR
constexpr int32_t kSceKernelErrorIllegalMemBlockType = static_cast<int32_t>(0x800200D7u); // SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE
constexpr int32_t kSceKernelErrorIllegalMemSize = static_cast<int32_t>(0x800200D8u);   // SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE
constexpr int32_t kSceKernelErrorNoMemory = static_cast<int32_t>(0x800200D9u);         // SCE_KERNEL_ERROR_NO_MEMORY
constexpr int32_t kSceKernelErrorIllegalPartitionId = static_cast<int32_t>(0x800200DCu); // SCE_KERNEL_ERROR_ILLEGAL_PARTITION_ID

// ThreadMan (uOFW)
constexpr int32_t kSceKernelErrorUnknownThid = static_cast<int32_t>(0x80020198u);      // SCE_KERNEL_ERROR_UNKNOWN_THID
constexpr int32_t kSceKernelErrorUnknownSemid = static_cast<int32_t>(0x80020199u);     // SCE_KERNEL_ERROR_UNKNOWN_SEMID
constexpr int32_t kSceKernelErrorUnknownEvfid = static_cast<int32_t>(0x8002019Au);     // SCE_KERNEL_ERROR_UNKNOWN_EVFID
constexpr int32_t kSceKernelErrorUnknownCbid = static_cast<int32_t>(0x8002019Du);      // SCE_KERNEL_ERROR_UNKNOWN_CBID
constexpr int32_t kSceKernelErrorNotDormant = static_cast<int32_t>(0x8002019Fu);       // SCE_KERNEL_ERROR_NOT_DORMANT
constexpr int32_t kSceKernelErrorDormant = static_cast<int32_t>(0x800201A0u);          // SCE_KERNEL_ERROR_DORMANT
constexpr int32_t kSceKernelErrorIllegalPriority = static_cast<int32_t>(0x800201A3u);  // SCE_KERNEL_ERROR_ILLEGAL_PRIORITY
constexpr int32_t kSceKernelErrorIllegalStackSize = static_cast<int32_t>(0x800201A4u); // SCE_KERNEL_ERROR_ILLEGAL_STACK_SIZE
constexpr int32_t kSceKernelErrorSemaZero = static_cast<int32_t>(0x800201A6u);         // SCE_KERNEL_ERROR_SEMA_ZERO
constexpr int32_t kSceKernelErrorEvfCond = static_cast<int32_t>(0x800201A7u);          // SCE_KERNEL_ERROR_EVF_COND
constexpr int32_t kSceKernelErrorWaitTimeout = static_cast<int32_t>(0x800201A8u);      // SCE_KERNEL_ERROR_WAIT_TIMEOUT
constexpr int32_t kSceKernelErrorWaitCancel = static_cast<int32_t>(0x800201A9u);       // SCE_KERNEL_ERROR_WAIT_CANCEL
constexpr int32_t kSceKernelErrorReleaseWait = static_cast<int32_t>(0x800201AAu);      // SCE_KERNEL_ERROR_RELEASE_WAIT
constexpr int32_t kSceKernelErrorWaitDelete = static_cast<int32_t>(0x800201B5u);       // SCE_KERNEL_ERROR_WAIT_DELETE
constexpr int32_t kSceKernelErrorSemaOvf = static_cast<int32_t>(0x800201B8u);          // SCE_KERNEL_ERROR_SEMA_OVF

// IoFileMgr / POSIX Errno (PSPSDK pspiofilemgr_kernel.h, uOFW)
constexpr int32_t kSceKernelErrorBadf = static_cast<int32_t>(0x80020323u);             // SCE_KERNEL_ERROR_BADF
constexpr int32_t kSceKernelErrorAsyncBusy = static_cast<int32_t>(0x80020329u);        // SCE_KERNEL_ERROR_ASYNC_BUSY
constexpr int32_t kSceKernelErrorErrnoEnoent = static_cast<int32_t>(0x80010002u);      // SCE_ERROR_ERRNO_ENOENT
constexpr int32_t kSceKernelErrorErrnoEacces = static_cast<int32_t>(0x8001000Du);      // SCE_ERROR_ERRNO_EACCES
constexpr int32_t kSceKernelErrorErrnoEexist = static_cast<int32_t>(0x80010011u);      // SCE_ERROR_ERRNO_EEXIST
constexpr int32_t kSceKernelErrorErrnoEinval = static_cast<int32_t>(0x80010016u);      // SCE_ERROR_ERRNO_EINVAL

// Common / Ctrl / Dmac errors (uOFW include/common/errors.h)
constexpr int32_t kSceKernelErrorInvalidPointer = static_cast<int32_t>(0x80000103u);  // SCE_ERROR_INVALID_POINTER
constexpr int32_t kSceKernelErrorInvalidSize = static_cast<int32_t>(0x80000104u);     // SCE_ERROR_INVALID_SIZE
constexpr int32_t kSceKernelErrorInvalidMode = static_cast<int32_t>(0x80000107u);     // SCE_ERROR_INVALID_MODE
constexpr int32_t kSceKernelErrorInvalidValue = static_cast<int32_t>(0x800001FEu);    // SCE_ERROR_INVALID_VALUE

} // namespace repsp::hle

#endif // REPSP_HLE_KERNEL_ERRORS_H
