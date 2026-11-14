// Public PSP SDK function name -> NID table, computed via SHA-1 at startup.
// Sources: PSPSDK public headers (https://github.com/pspdev/pspsdk).

#include "repsp/runtime/nid_names.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/sha1.h"

#include <cstdio>

namespace repsp::runtime
{

namespace
{

struct RawNidDef
{
    const char* library;
    const char* name;
};

constexpr RawNidDef kKnownNames[] = {
    // SysMemUserForUser
    { "SysMemUserForUser", "sceKernelAllocPartitionMemory" },
    { "SysMemUserForUser", "sceKernelFreePartitionMemory" },
    { "SysMemUserForUser", "sceKernelGetBlockHeadAddr" },
    { "SysMemUserForUser", "sceKernelMaxFreeMemSize" },
    { "SysMemUserForUser", "sceKernelTotalFreeMemSize" },
    { "SysMemUserForUser", "sceKernelDevkitVersion" },
    { "SysMemUserForUser", "sceKernelPrintf" },

    // ThreadManForUser
    { "ThreadManForUser", "sceKernelCreateThread" },
    { "ThreadManForUser", "sceKernelStartThread" },
    { "ThreadManForUser", "sceKernelExitThread" },
    { "ThreadManForUser", "sceKernelExitDeleteThread" },
    { "ThreadManForUser", "sceKernelDeleteThread" },
    { "ThreadManForUser", "sceKernelTerminateThread" },
    { "ThreadManForUser", "sceKernelTerminateDeleteThread" },
    { "ThreadManForUser", "sceKernelWaitThreadEnd" },
    { "ThreadManForUser", "sceKernelWaitThreadEndCB" },
    { "ThreadManForUser", "sceKernelGetThreadId" },
    { "ThreadManForUser", "sceKernelGetThreadCurrentPriority" },
    { "ThreadManForUser", "sceKernelChangeThreadPriority" },
    { "ThreadManForUser", "sceKernelSleepThread" },
    { "ThreadManForUser", "sceKernelSleepThreadCB" },
    { "ThreadManForUser", "sceKernelWakeupThread" },
    { "ThreadManForUser", "sceKernelDelayThread" },
    { "ThreadManForUser", "sceKernelDelayThreadCB" },
    { "ThreadManForUser", "sceKernelCreateSema" },
    { "ThreadManForUser", "sceKernelDeleteSema" },
    { "ThreadManForUser", "sceKernelSignalSema" },
    { "ThreadManForUser", "sceKernelWaitSema" },
    { "ThreadManForUser", "sceKernelWaitSemaCB" },
    { "ThreadManForUser", "sceKernelPollSema" },
    { "ThreadManForUser", "sceKernelCreateEventFlag" },
    { "ThreadManForUser", "sceKernelDeleteEventFlag" },
    { "ThreadManForUser", "sceKernelSetEventFlag" },
    { "ThreadManForUser", "sceKernelClearEventFlag" },
    { "ThreadManForUser", "sceKernelWaitEventFlag" },
    { "ThreadManForUser", "sceKernelWaitEventFlagCB" },
    { "ThreadManForUser", "sceKernelPollEventFlag" },
    { "ThreadManForUser", "sceKernelReferEventFlagStatus" },
    { "ThreadManForUser", "sceKernelCreateCallback" },
    { "ThreadManForUser", "sceKernelDeleteCallback" },
    { "ThreadManForUser", "sceKernelNotifyCallback" },
    { "ThreadManForUser", "sceKernelCheckCallback" },
    { "ThreadManForUser", "sceKernelGetSystemTimeLow" },
    { "ThreadManForUser", "sceKernelGetSystemTimeWide" },

    // IoFileMgrForUser
    { "IoFileMgrForUser", "sceIoOpen" },
    { "IoFileMgrForUser", "sceIoOpenAsync" },
    { "IoFileMgrForUser", "sceIoClose" },
    { "IoFileMgrForUser", "sceIoCloseAsync" },
    { "IoFileMgrForUser", "sceIoRead" },
    { "IoFileMgrForUser", "sceIoReadAsync" },
    { "IoFileMgrForUser", "sceIoWrite" },
    { "IoFileMgrForUser", "sceIoWriteAsync" },
    { "IoFileMgrForUser", "sceIoLseek" },
    { "IoFileMgrForUser", "sceIoLseekAsync" },
    { "IoFileMgrForUser", "sceIoLseek32" },
    { "IoFileMgrForUser", "sceIoGetstat" },
    { "IoFileMgrForUser", "sceIoWaitAsync" },
    { "IoFileMgrForUser", "sceIoWaitAsyncCB" },
    { "IoFileMgrForUser", "sceIoPollAsync" },
    { "IoFileMgrForUser", "sceIoDopen" },
    { "IoFileMgrForUser", "sceIoDread" },
    { "IoFileMgrForUser", "sceIoDclose" },
    { "IoFileMgrForUser", "sceIoChdir" },
    { "IoFileMgrForUser", "sceIoMkdir" },

    // ModuleMgrForUser
    { "ModuleMgrForUser", "sceKernelLoadModule" },
    { "ModuleMgrForUser", "sceKernelStartModule" },
    { "ModuleMgrForUser", "sceKernelStopModule" },
    { "ModuleMgrForUser", "sceKernelUnloadModule" },
    { "ModuleMgrForUser", "sceKernelSelfStopUnloadModule" },

    // LoadExecForUser
    { "LoadExecForUser", "sceKernelExitGame" },
    { "LoadExecForUser", "sceKernelRegisterExitCallback" },

    // UtilsForUser
    { "UtilsForUser", "sceKernelDcacheWritebackAll" },
    { "UtilsForUser", "sceKernelDcacheWritebackInvalidateAll" },
    { "UtilsForUser", "sceKernelDcacheWritebackRange" },
    { "UtilsForUser", "sceKernelDcacheWritebackInvalidateRange" },
    { "UtilsForUser", "sceKernelIcacheInvalidateAll" },
    { "UtilsForUser", "sceKernelUtilsMt19937Init" },
    { "UtilsForUser", "sceKernelUtilsMt19937UInt" },
    { "UtilsForUser", "sceKernelSetGPO" },
    { "UtilsForUser", "sceKernelGetGPI" },

    // StdioForUser
    { "StdioForUser", "sceKernelStdin" },
    { "StdioForUser", "sceKernelStdout" },
    { "StdioForUser", "sceKernelStderr" },

    // Kernel_Library
    { "Kernel_Library", "sceKernelCpuSuspendIntr" },
    { "Kernel_Library", "sceKernelCpuResumeIntr" },
    { "Kernel_Library", "sceKernelCpuResumeIntrWithSync" },
    { "Kernel_Library", "sceKernelIsCpuIntrSuspended" },
    { "Kernel_Library", "sceKernelIsCpuIntrEnable" },

    // scePower
    { "scePower", "scePowerRegisterCallback" },
    { "scePower", "scePowerUnregisterCallback" },
    { "scePower", "scePowerSetClockFrequency" },
    { "scePower", "scePowerTick" },

    // sceSuspendForUser
    { "sceSuspendForUser", "sceKernelPowerLock" },
    { "sceSuspendForUser", "sceKernelPowerUnlock" },
    { "sceSuspendForUser", "sceKernelPowerTick" },

    // sceUmdUser
    { "sceUmdUser", "sceUmdCheckMedium" },
    { "sceUmdUser", "sceUmdActivate" },
    { "sceUmdUser", "sceUmdDeactivate" },
    { "sceUmdUser", "sceUmdWaitDriveStat" },
    { "sceUmdUser", "sceUmdWaitDriveStatWithTimer" },
    { "sceUmdUser", "sceUmdWaitDriveStatCB" },
    { "sceUmdUser", "sceUmdCancelWaitDriveStat" },
    { "sceUmdUser", "sceUmdGetDriveStat" },
    { "sceUmdUser", "sceUmdGetErrorStat" },
    { "sceUmdUser", "sceUmdRegisterUMDCallBack" },
    { "sceUmdUser", "sceUmdUnRegisterUMDCallBack" },

    // sceGe_user
    { "sceGe_user", "sceGeEdramGetAddr" },
    { "sceGe_user", "sceGeEdramGetSize" },
    { "sceGe_user", "sceGeSetCallback" },
    { "sceGe_user", "sceGeUnsetCallback" },
    { "sceGe_user", "sceGeListEnQueue" },
    { "sceGe_user", "sceGeListEnQueueHead" },
    { "sceGe_user", "sceGeListDeQueue" },
    { "sceGe_user", "sceGeListUpdateStallAddr" },
    { "sceGe_user", "sceGeListSync" },
    { "sceGe_user", "sceGeDrawSync" },
    { "sceGe_user", "sceGeBreak" },
    { "sceGe_user", "sceGeContinue" },

    // sceDisplay
    { "sceDisplay", "sceDisplaySetMode" },
    { "sceDisplay", "sceDisplayGetMode" },
    { "sceDisplay", "sceDisplaySetFrameBuf" },
    { "sceDisplay", "sceDisplayGetFrameBuf" },
    { "sceDisplay", "sceDisplayWaitVblankStart" },
    { "sceDisplay", "sceDisplayWaitVblankStartCB" },
    { "sceDisplay", "sceDisplayWaitVblank" },
    { "sceDisplay", "sceDisplayWaitVblankCB" },
    { "sceDisplay", "sceDisplayGetVcount" },
    { "sceDisplay", "sceDisplayGetFramePerSec" },

    // sceCtrl
    { "sceCtrl", "sceCtrlSetSamplingCycle" },
    { "sceCtrl", "sceCtrlGetSamplingCycle" },
    { "sceCtrl", "sceCtrlSetSamplingMode" },
    { "sceCtrl", "sceCtrlGetSamplingMode" },
    { "sceCtrl", "sceCtrlReadBufferPositive" },
    { "sceCtrl", "sceCtrlReadBufferNegative" },
    { "sceCtrl", "sceCtrlPeekBufferPositive" },
    { "sceCtrl", "sceCtrlPeekBufferNegative" },
    { "sceCtrl", "sceCtrlReadLatch" },

    // sceAudio
    { "sceAudio", "sceAudioChReserve" },
    { "sceAudio", "sceAudioChRelease" },
    { "sceAudio", "sceAudioOutput" },
    { "sceAudio", "sceAudioOutputBlocking" },
    { "sceAudio", "sceAudioOutputPanned" },
    { "sceAudio", "sceAudioOutputPannedBlocking" },
    { "sceAudio", "sceAudioChangeChannelConfig" },
    { "sceAudio", "sceAudioChangeChannelVolume" },
    { "sceAudio", "sceAudioGetChannelRestLen" },
    { "sceAudio", "sceAudioSetChannelDataLen" },
    { "sceAudio", "sceAudioSRCChReserve" },
    { "sceAudio", "sceAudioSRCChRelease" },
    { "sceAudio", "sceAudioSRCOutputBlocking" },

    // sceUtility
    { "sceUtility", "sceUtilityGetSystemParamInt" },
    { "sceUtility", "sceUtilityGetSystemParamString" },
    { "sceUtility", "sceUtilityMsgDialogInitStart" },
    { "sceUtility", "sceUtilityMsgDialogShutdownStart" },
    { "sceUtility", "sceUtilityMsgDialogGetStatus" },
    { "sceUtility", "sceUtilityMsgDialogUpdate" },
    { "sceUtility", "sceUtilitySavedataInitStart" },
    { "sceUtility", "sceUtilitySavedataShutdownStart" },
    { "sceUtility", "sceUtilitySavedataGetStatus" },
    { "sceUtility", "sceUtilitySavedataUpdate" },
    { "sceUtility", "sceUtilityLoadModule" },
    { "sceUtility", "sceUtilityUnloadModule" },

    // sceRtc
    { "sceRtc", "sceRtcGetCurrentClock" },
    { "sceRtc", "sceRtcGetCurrentClockLocalTime" },
    { "sceRtc", "sceRtcGetCurrentTick" },
    { "sceRtc", "sceRtcGetTickResolution" },
};

std::unordered_map<uint32_t, NidNameEntry> BuildKnownNidTable()
{
    std::unordered_map<uint32_t, NidNameEntry> table;
    for (const auto& def : kKnownNames)
    {
        const uint32_t nid = ComputeNid(def.name);
        auto it = table.find(nid);
        if (it == table.end())
        {
            table.emplace(nid, NidNameEntry{nid, def.library, def.name, false});
        }
        else if (it->second.name != def.name || it->second.library != def.library)
        {
            Log(LogLevel::kWarn, "[NID] Collision on 0x%08X: %s:%s vs %s:%s",
                nid, it->second.library.c_str(), it->second.name.c_str(), def.library, def.name);
            it->second.library += " / ";
            it->second.library += def.library;
            it->second.name += " / ";
            it->second.name += def.name;
            it->second.collision = true;
        }
    }
    return table;
}

} // namespace

const std::unordered_map<uint32_t, NidNameEntry>& GetKnownNidTable()
{
    static const std::unordered_map<uint32_t, NidNameEntry> kTable = BuildKnownNidTable();
    return kTable;
}

const NidNameEntry* LookupKnownNid(uint32_t nid)
{
    const auto& table = GetKnownNidTable();
    auto it = table.find(nid);
    return (it != table.end()) ? &it->second : nullptr;
}

std::string FormatKnownNid(uint32_t nid)
{
    char buf[160];
    if (const NidNameEntry* entry = LookupKnownNid(nid))
    {
        std::snprintf(buf, sizeof(buf), "%s:%s (0x%08X)",
                      entry->library.c_str(), entry->name.c_str(), nid);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "0x%08X", nid);
    }
    return std::string(buf);
}

} // namespace repsp::runtime
