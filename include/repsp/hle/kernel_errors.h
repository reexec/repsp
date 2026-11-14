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

// ModuleMgr errors (uOFW)
constexpr int32_t kSceKernelErrorPrxTypeUnsupported = static_cast<int32_t>(0x80020148u); // SCE_KERNEL_ERROR_PRX_TYPE_UNSUPPORTED

// sceAudio errors (uOFW kd/audio, PSPSDK pspaudio.h)
constexpr int32_t kSceAudioErrorNotInitialized = static_cast<int32_t>(0x80260001u);   // SCE_AUDIO_ERROR_NOT_INITIALIZED
constexpr int32_t kSceAudioErrorOutputBusy = static_cast<int32_t>(0x80260002u);       // SCE_AUDIO_ERROR_OUTPUT_BUSY
constexpr int32_t kSceAudioErrorInvalidCh = static_cast<int32_t>(0x80260003u);        // SCE_AUDIO_ERROR_INVALID_CH
constexpr int32_t kSceAudioErrorPrivRequired = static_cast<int32_t>(0x80260004u);     // SCE_AUDIO_ERROR_PRIV_REQUIRED
constexpr int32_t kSceAudioErrorNotFound = static_cast<int32_t>(0x80260005u);         // SCE_AUDIO_ERROR_NOT_FOUND
constexpr int32_t kSceAudioErrorInvalidSize = static_cast<int32_t>(0x80260006u);      // SCE_AUDIO_ERROR_INVALID_SIZE
constexpr int32_t kSceAudioErrorInvalidFormat = static_cast<int32_t>(0x80260007u);    // SCE_AUDIO_ERROR_INVALID_FORMAT
constexpr int32_t kSceAudioErrorNotReserved = static_cast<int32_t>(0x80260008u);      // SCE_AUDIO_ERROR_NOT_RESERVED
constexpr int32_t kSceAudioErrorNotOutput = static_cast<int32_t>(0x80260009u);        // SCE_AUDIO_ERROR_NOT_OUTPUT
constexpr int32_t kSceAudioErrorInvalidFrequency = static_cast<int32_t>(0x8026000Au); // SCE_AUDIO_ERROR_INVALID_FREQUENCY
constexpr int32_t kSceAudioErrorInvalidVolume = static_cast<int32_t>(0x8026000Bu);    // SCE_AUDIO_ERROR_INVALID_VOLUME

// sceSasCore errors (public Sony/PSP SDK facility 0x8042)
constexpr int32_t kSceSasErrorInvalidGrain = static_cast<int32_t>(0x80420001u);       // SCE_SAS_ERROR_NSMPL / INVALID_GRAIN
constexpr int32_t kSceSasErrorInvalidMaxVoices = static_cast<int32_t>(0x80420002u);   // SCE_SAS_ERROR_INVALID_MAX_VOICES
constexpr int32_t kSceSasErrorInvalidVoice = static_cast<int32_t>(0x80420010u);       // SCE_SAS_ERROR_INVALID_VOICE
constexpr int32_t kSceSasErrorInvalidParameter = static_cast<int32_t>(0x80420014u);   // SCE_SAS_ERROR_ADPCM_SIZE / INVALID_PARAMETER
constexpr int32_t kSceSasErrorInvalidFxType = static_cast<int32_t>(0x80420020u);      // SCE_SAS_ERROR_FX_TYPE

// sceAtrac3plus errors (PSPSDK pspatrac3.h)
constexpr int32_t kSceAtracErrorParamFail = static_cast<int32_t>(0x80630001u);            // PSP_ATRAC_ERROR_PARAM_FAIL
constexpr int32_t kSceAtracErrorApiFail = static_cast<int32_t>(0x80630002u);              // PSP_ATRAC_ERROR_API_FAIL
constexpr int32_t kSceAtracErrorNoAtracId = static_cast<int32_t>(0x80630003u);            // PSP_ATRAC_ERROR_NO_ATRACID
constexpr int32_t kSceAtracErrorBadCodecType = static_cast<int32_t>(0x80630004u);         // PSP_ATRAC_ERROR_BAD_CODECTYPE
constexpr int32_t kSceAtracErrorBadAtracId = static_cast<int32_t>(0x80630005u);           // PSP_ATRAC_ERROR_BAD_ATRACID
constexpr int32_t kSceAtracErrorUnknownFormat = static_cast<int32_t>(0x80630006u);         // PSP_ATRAC_ERROR_UNKNOWN_FORMAT
constexpr int32_t kSceAtracErrorUnmatchFormat = static_cast<int32_t>(0x80630007u);         // PSP_ATRAC_ERROR_UNMATCH_FORMAT
constexpr int32_t kSceAtracErrorBadData = static_cast<int32_t>(0x80630008u);               // PSP_ATRAC_ERROR_BAD_DATA
constexpr int32_t kSceAtracErrorAllDataIsOnMemory = static_cast<int32_t>(0x80630009u);    // PSP_ATRAC_ERROR_ALLDATA_IS_ONMEMORY
constexpr int32_t kSceAtracErrorUnsetData = static_cast<int32_t>(0x80630010u);             // PSP_ATRAC_ERROR_UNSET_DATA
constexpr int32_t kSceAtracErrorReadSizeIsTooSmall = static_cast<int32_t>(0x80630011u);    // PSP_ATRAC_ERROR_READSIZE_IS_TOO_SMALL
constexpr int32_t kSceAtracErrorNeedSecondBuffer = static_cast<int32_t>(0x80630012u);      // PSP_ATRAC_ERROR_NEED_SECOND_BUFFER
constexpr int32_t kSceAtracErrorReadSizeOverBuffer = static_cast<int32_t>(0x80630013u);    // PSP_ATRAC_ERROR_READSIZE_OVER_BUFFER
constexpr int32_t kSceAtracErrorNot4ByteAlignment = static_cast<int32_t>(0x80630014u);    // PSP_ATRAC_ERROR_NOT_4BYTE_ALIGNMENT
constexpr int32_t kSceAtracErrorBadSample = static_cast<int32_t>(0x80630015u);             // PSP_ATRAC_ERROR_BAD_SAMPLE
constexpr int32_t kSceAtracErrorWriteByteFirstBuffer = static_cast<int32_t>(0x80630016u); // PSP_ATRAC_ERROR_WRITEBYTE_FIRST_BUFFER
constexpr int32_t kSceAtracErrorWriteByteSecondBuffer = static_cast<int32_t>(0x80630017u);// PSP_ATRAC_ERROR_WRITEBYTE_SECOND_BUFFER
constexpr int32_t kSceAtracErrorAddDataIsTooBig = static_cast<int32_t>(0x80630018u);      // PSP_ATRAC_ERROR_ADD_DATA_IS_TOO_BIG
constexpr int32_t kSceAtracErrorUnsetParam = static_cast<int32_t>(0x80630021u);            // PSP_ATRAC_ERROR_UNSET_PARAM
constexpr int32_t kSceAtracErrorNoNeedSecondBuffer = static_cast<int32_t>(0x80630022u);  // PSP_ATRAC_ERROR_NONEED_SECOND_BUFFER
constexpr int32_t kSceAtracErrorNoDataInBuffer = static_cast<int32_t>(0x80630023u);      // PSP_ATRAC_ERROR_NODATA_IN_BUFFER
constexpr int32_t kSceAtracErrorAllDataWasDecoded = static_cast<int32_t>(0x80630024u);   // PSP_ATRAC_ERROR_ALLDATA_WAS_DECODED

// sceMpeg errors (Sony/PSP SDK facility 0x8061; NO_DATA 0x80618001 verified in guest binary)
constexpr int32_t kSceMpegErrorBadVersion = static_cast<int32_t>(0x80610002u);             // SCE_MPEG_ERROR_BAD_VERSION
constexpr int32_t kSceMpegErrorNoMemory = static_cast<int32_t>(0x80610022u);               // SCE_MPEG_ERROR_NO_MEM
constexpr int32_t kSceMpegErrorInvalidAddr = static_cast<int32_t>(0x80610103u);            // SCE_MPEG_ERROR_INVALID_ADDR
constexpr int32_t kSceMpegErrorInvalidValue = static_cast<int32_t>(0x806101FEu);           // SCE_MPEG_ERROR_INVALID_VALUE
constexpr int32_t kSceMpegErrorNoData = static_cast<int32_t>(0x80618001u);                 // SCE_MPEG_ERROR_NO_DATA
constexpr int32_t kSceMpegErrorAlreadyInit = static_cast<int32_t>(0x80618005u);            // SCE_MPEG_ERROR_ALREADY_INIT

} // namespace repsp::hle

#endif // REPSP_HLE_KERNEL_ERRORS_H
