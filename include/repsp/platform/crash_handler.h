#ifndef REPSP_PLATFORM_CRASH_HANDLER_H
#define REPSP_PLATFORM_CRASH_HANDLER_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace repsp::platform
{

struct CrashTextBuffer
{
    char* data = nullptr;
    size_t capacity = 0;
    size_t length = 0;

    void Append(const char* str);
    void AppendBytes(const char* str, size_t count);
    void AppendFormat(const char* fmt, ...);
};

using CrashExtraCallback = void (*)(void* user_data, CrashTextBuffer* out);
using CrashCleanupCallback = void (*)(void* user_data);

struct CrashConfig
{
    std::string crash_dir;
    bool write_minidump = true;
    CrashExtraCallback extra_callback = nullptr;
    void* extra_user_data = nullptr;
};

struct LogFileLimits
{
    uint64_t max_bytes = 4ull * 1024ull * 1024ull;
    uint32_t max_files = 5;
};

void InstallCrashHandler(const CrashConfig& config);
void UninstallCrashHandler();

void SetCrashReportExtra(CrashExtraCallback cb, void* user_data);
void SetCrashGuestMemoryRange(uintptr_t base, size_t size);
void RegisterCrashStateProvider(CrashExtraCallback cb, void* user_data);
void RegisterCrashCleanupCallback(CrashCleanupCallback cb, void* user_data);

std::string GetLastCrashReportPath();
std::string GetLastMinidumpPath();

bool SetLogFile(const std::string& path, const LogFileLimits& limits = {});
void WriteLogFileLine(const char* level, const char* message);
void FlushLogFile();
void CloseLogFile();

uint64_t GetCurrentHostThreadId();

} // namespace repsp::platform

#endif // REPSP_PLATFORM_CRASH_HANDLER_H
