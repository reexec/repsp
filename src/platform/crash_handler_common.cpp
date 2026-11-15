#include "crash_handler_internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace repsp::platform
{

void CrashTextBuffer::AppendBytes(const char* str, size_t count)
{
    if (data == nullptr || capacity == 0 || str == nullptr || count == 0)
    {
        return;
    }
    if (length + 1 >= capacity)
    {
        return;
    }
    const size_t avail = capacity - length - 1;
    const size_t to_copy = (count < avail) ? count : avail;
    std::memcpy(data + length, str, to_copy);
    length += to_copy;
    data[length] = '\0';
}

void CrashTextBuffer::Append(const char* str)
{
    if (str == nullptr)
    {
        return;
    }
    AppendBytes(str, std::strlen(str));
}

void CrashTextBuffer::AppendFormat(const char* fmt, ...)
{
    if (data == nullptr || capacity == 0 || fmt == nullptr || length + 1 >= capacity)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(data + length, capacity - length, fmt, args);
    va_end(args);
    if (n > 0)
    {
        const size_t added = static_cast<size_t>(n);
        if (length + added >= capacity)
        {
            length = capacity - 1;
        }
        else
        {
            length += added;
        }
        data[length] = '\0';
    }
}

namespace detail
{

CrashSharedState& GetCrashSharedState()
{
    static CrashSharedState state;
    return state;
}

void FormatUtcTimestamp(char* out, size_t cap)
{
    if (out == nullptr || cap == 0)
    {
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm tm_val{};
#ifdef _WIN32
    gmtime_s(&tm_val, &now);
#else
    gmtime_r(&now, &tm_val);
#endif
    std::snprintf(out, cap, "%04d%02d%02d-%02d%02d%02d",
                  tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                  tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
}

void PrepareCrashOutputPaths(const char* crash_dir)
{
    auto& s = GetCrashSharedState();
    s.crash_dir[0] = '\0';
    s.report_path[0] = '\0';
    s.minidump_path[0] = '\0';

    if (crash_dir == nullptr || *crash_dir == '\0')
    {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(crash_dir, ec);

    std::snprintf(s.crash_dir, sizeof(s.crash_dir), "%s", crash_dir);

    char ts[32]{};
    FormatUtcTimestamp(ts, sizeof(ts));

    const char sep =
#ifdef _WIN32
        '\\';
#else
        '/';
#endif
    const size_t dir_len = std::strlen(s.crash_dir);
    const bool has_sep = (dir_len > 0 && (s.crash_dir[dir_len - 1] == '/' || s.crash_dir[dir_len - 1] == '\\'));

    if (has_sep)
    {
        std::snprintf(s.report_path, sizeof(s.report_path), "%scrash_report_%s.txt", s.crash_dir, ts);
        std::snprintf(s.minidump_path, sizeof(s.minidump_path), "%scrash_dump_%s.dmp", s.crash_dir, ts);
    }
    else
    {
        std::snprintf(s.report_path, sizeof(s.report_path), "%s%ccrash_report_%s.txt", s.crash_dir, sep, ts);
        std::snprintf(s.minidump_path, sizeof(s.minidump_path), "%s%ccrash_dump_%s.dmp", s.crash_dir, sep, ts);
    }
}

void RunCrashCleanupCallbacks()
{
    auto& s = GetCrashSharedState();
    for (size_t i = 0; i < s.cleanup_count && i < CrashSharedState::kMaxCleanupCallbacks; ++i)
    {
        if (s.cleanup_cbs[i] != nullptr)
        {
            s.cleanup_cbs[i](s.cleanup_user_data[i]);
        }
    }
}

void FormatCrashReportHeader(CrashTextBuffer* out, const CrashTerminationInfo& info, uint64_t host_tid)
{
    char ts[32]{};
    FormatUtcTimestamp(ts, sizeof(ts));

    out->Append("=== REPSP CRASH REPORT ===\n");
    out->AppendFormat("timestamp_utc: %s\n", ts);
    out->AppendFormat("termination_kind: %s\n", info.kind ? info.kind : "unknown");
    out->AppendFormat("code: 0x%08llX\n", static_cast<unsigned long long>(info.code));
    out->AppendFormat("fault_address: 0x%016llX\n", static_cast<unsigned long long>(info.address));
    if (info.detail != nullptr && *info.detail != '\0')
    {
        out->AppendFormat("detail: %s\n", info.detail);
    }
    out->AppendFormat("crashing_host_tid: %llu\n", static_cast<unsigned long long>(host_tid));
}

} // namespace detail

void SetCrashReportExtra(CrashExtraCallback cb, void* user_data)
{
    auto& s = detail::GetCrashSharedState();
    s.extra_cb = cb;
    s.extra_user_data = user_data;
}

void SetCrashGuestMemoryRange(uintptr_t base, size_t size)
{
    auto& s = detail::GetCrashSharedState();
    s.guest_mem_base = base;
    s.guest_mem_size = size;
}

void RegisterCrashStateProvider(CrashExtraCallback cb, void* user_data)
{
    auto& s = detail::GetCrashSharedState();
    s.state_provider_cb = cb;
    s.state_provider_user_data = user_data;
}

void RegisterCrashCleanupCallback(CrashCleanupCallback cb, void* user_data)
{
    if (cb == nullptr)
    {
        return;
    }
    auto& s = detail::GetCrashSharedState();
    for (size_t i = 0; i < s.cleanup_count; ++i)
    {
        if (s.cleanup_cbs[i] == cb && s.cleanup_user_data[i] == user_data)
        {
            return;
        }
    }
    if (s.cleanup_count < detail::CrashSharedState::kMaxCleanupCallbacks)
    {
        s.cleanup_cbs[s.cleanup_count] = cb;
        s.cleanup_user_data[s.cleanup_count] = user_data;
        ++s.cleanup_count;
    }
}

std::string GetLastCrashReportPath()
{
    return detail::GetCrashSharedState().report_path;
}

std::string GetLastMinidumpPath()
{
    return detail::GetCrashSharedState().minidump_path;
}

uint64_t GetCurrentHostThreadId()
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__linux__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#else
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
}

namespace
{

struct LogFileState
{
    std::mutex mutex;
    std::string path;
    LogFileLimits limits;
    uint64_t bytes_written = 0;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

LogFileState& GetLogFileState()
{
    static LogFileState state;
    return state;
}

void CloseLogHandleLocked(LogFileState& st)
{
#ifdef _WIN32
    if (st.handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(st.handle);
        st.handle = INVALID_HANDLE_VALUE;
    }
#else
    if (st.fd >= 0)
    {
        close(st.fd);
        st.fd = -1;
    }
#endif
}

void PruneOldLogs(const std::filesystem::path& log_path, uint32_t max_files)
{
    if (max_files == 0)
    {
        return;
    }
    const std::filesystem::path dir = log_path.parent_path();
    const std::string stem = log_path.stem().string();
    const std::string ext = log_path.extension().string();
    if (dir.empty())
    {
        return;
    }
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> matching;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec))
        {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(stem, 0) == 0 && entry.path().extension().string() == ext)
        {
            matching.push_back(entry);
        }
    }
    if (matching.size() <= max_files)
    {
        return;
    }
    std::sort(matching.begin(), matching.end(),
              [](const auto& a, const auto& b)
              {
                  std::error_code e1, e2;
                  return a.last_write_time(e1) < b.last_write_time(e2);
              });
    const size_t to_remove = matching.size() - max_files;
    for (size_t i = 0; i < to_remove; ++i)
    {
        std::filesystem::remove(matching[i].path(), ec);
    }
}

void OpenLogHandleLocked(LogFileState& st)
{
    CloseLogHandleLocked(st);
    st.bytes_written = 0;
    if (st.path.empty())
    {
        return;
    }
    const std::filesystem::path p(st.path);
    std::error_code ec;
    if (!p.parent_path().empty())
    {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    PruneOldLogs(p, st.limits.max_files);

#ifdef _WIN32
    st.handle = CreateFileA(
        st.path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
#else
    st.fd = open(st.path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
}

void WriteRawBytes(LogFileState& st, const char* data, size_t len)
{
    if (data == nullptr || len == 0)
    {
        return;
    }
#ifdef _WIN32
    if (st.handle != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(st.handle, data, static_cast<DWORD>(len), &written, nullptr);
        st.bytes_written += written;
    }
#else
    if (st.fd >= 0)
    {
        const ssize_t n = write(st.fd, data, len);
        if (n > 0)
        {
            st.bytes_written += static_cast<uint64_t>(n);
        }
    }
#endif
}

} // namespace

bool SetLogFile(const std::string& path, const LogFileLimits& limits)
{
    auto& st = GetLogFileState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.path = path;
    st.limits = limits;
    OpenLogHandleLocked(st);
#ifdef _WIN32
    return path.empty() || st.handle != INVALID_HANDLE_VALUE;
#else
    return path.empty() || st.fd >= 0;
#endif
}

void WriteLogFileLine(const char* level, const char* message)
{
    auto& st = GetLogFileState();
    std::lock_guard<std::mutex> lock(st.mutex);
#ifdef _WIN32
    if (st.handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
#else
    if (st.fd < 0)
    {
        return;
    }
#endif
    if (st.limits.max_bytes > 0 && st.bytes_written >= st.limits.max_bytes)
    {
        return;
    }

    char prefix[32];
    const int plen = std::snprintf(prefix, sizeof(prefix), "[%s] ", level ? level : "LOG");
    if (plen > 0)
    {
        WriteRawBytes(st, prefix, static_cast<size_t>(plen));
    }
    if (message != nullptr)
    {
        WriteRawBytes(st, message, std::strlen(message));
    }
    WriteRawBytes(st, "\n", 1);
}

void FlushLogFile()
{
    // No-op lock-free flush: writes go directly to OS file handle/fd.
    auto& st = GetLogFileState();
#ifdef _WIN32
    if (st.handle != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(st.handle);
    }
#else
    if (st.fd >= 0)
    {
        fsync(st.fd);
    }
#endif
}

void CloseLogFile()
{
    auto& st = GetLogFileState();
    std::lock_guard<std::mutex> lock(st.mutex);
    CloseLogHandleLocked(st);
}

} // namespace repsp::platform
