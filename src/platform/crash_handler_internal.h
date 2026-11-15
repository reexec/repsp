#ifndef REPSP_PLATFORM_CRASH_HANDLER_INTERNAL_H
#define REPSP_PLATFORM_CRASH_HANDLER_INTERNAL_H

#include "repsp/platform/crash_handler.h"

#include <atomic>
#include <cstdint>

namespace repsp::platform::detail
{

struct CrashTerminationInfo
{
    const char* kind = "unknown";
    uint64_t code = 0;
    uintptr_t address = 0;
    const char* detail = nullptr;
    void* native_exception_pointers = nullptr;
};

struct CrashSharedState
{
    std::atomic<bool> installed{false};
    std::atomic<bool> handling_crash{false};
    bool write_minidump = true;
    char crash_dir[512]{};
    char report_path[600]{};
    char minidump_path[600]{};
    uintptr_t guest_mem_base = 0;
    size_t guest_mem_size = 0;

    CrashExtraCallback extra_cb = nullptr;
    void* extra_user_data = nullptr;

    CrashExtraCallback state_provider_cb = nullptr;
    void* state_provider_user_data = nullptr;

    static constexpr size_t kMaxCleanupCallbacks = 8;
    CrashCleanupCallback cleanup_cbs[kMaxCleanupCallbacks]{};
    void* cleanup_user_data[kMaxCleanupCallbacks]{};
    size_t cleanup_count = 0;
};

CrashSharedState& GetCrashSharedState();

void PrepareCrashOutputPaths(const char* crash_dir);
void RunCrashCleanupCallbacks();
void FormatCrashReportHeader(CrashTextBuffer* out, const CrashTerminationInfo& info, uint64_t host_tid);
void FormatUtcTimestamp(char* out, size_t cap);

} // namespace repsp::platform::detail

#endif // REPSP_PLATFORM_CRASH_HANDLER_INTERNAL_H
