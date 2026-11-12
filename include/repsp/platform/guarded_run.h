#ifndef REPSP_PLATFORM_GUARDED_RUN_H
#define REPSP_PLATFORM_GUARDED_RUN_H

#include <cstdint>
#include <functional>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// C linkage stop function for recompiled/C code.
// Stops execution of the innermost active GuardedRun and returns the stop reason.
// CONSTRAINT: No C++ frames with destructors may reside between the caller
// and the nearest enclosing GuardedRun frame.
void repsp_raise_stop(const char* reason);

#ifdef __cplusplus
}
#endif

namespace repsp::platform
{

struct GuardedRunResult
{
    bool succeeded = false;
    bool stopped = false;
    std::string stop_reason;
    uint32_t native_exception_code = 0;
    uintptr_t fault_address = 0;
    std::string description;
};

// C++ API to stop innermost GuardedRun.
// Calls repsp_raise_stop internally.
[[noreturn]] void RaiseStop(const char* reason);

GuardedRunResult GuardedRun(const std::function<void()>& fn);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_GUARDED_RUN_H
