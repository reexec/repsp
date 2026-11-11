#ifndef REPSP_PLATFORM_GUARDED_RUN_H
#define REPSP_PLATFORM_GUARDED_RUN_H

#include <cstdint>
#include <functional>
#include <string>

namespace repsp::platform
{

struct GuardedRunResult
{
    bool succeeded = false;
    uint32_t native_exception_code = 0;
    uintptr_t fault_address = 0;
    std::string description;
};

GuardedRunResult GuardedRun(const std::function<void()>& fn);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_GUARDED_RUN_H
