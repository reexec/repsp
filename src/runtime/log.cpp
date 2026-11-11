#include "repsp/runtime/log.h"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace repsp::runtime
{

namespace
{

LogLevel g_log_level = LogLevel::kInfo;
LogCallback g_log_callback = nullptr;

const char* LevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::kDebug:
        return "DEBUG";
    case LogLevel::kInfo:
        return "INFO";
    case LogLevel::kWarn:
        return "WARN";
    case LogLevel::kError:
        return "ERROR";
    default:
        return "LOG";
    }
}

} // namespace

void Log(LogLevel level, const char* format, ...)
{
    if (level < g_log_level)
    {
        return;
    }

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (len < 0)
    {
        va_end(args);
        return;
    }

    std::vector<char> buffer(static_cast<size_t>(len) + 1);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    if (g_log_callback != nullptr)
    {
        g_log_callback(level, buffer.data());
    }
    else
    {
        std::fprintf(stderr, "[%s] %s\n", LevelToString(level), buffer.data());
    }
}

void SetLogLevel(LogLevel level)
{
    g_log_level = level;
}

LogLevel GetLogLevel()
{
    return g_log_level;
}

void SetLogCallback(LogCallback callback)
{
    g_log_callback = callback;
}

} // namespace repsp::runtime
