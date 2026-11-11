#ifndef REPSP_RUNTIME_LOG_H
#define REPSP_RUNTIME_LOG_H

namespace repsp::runtime
{

enum class LogLevel
{
    kDebug,
    kInfo,
    kWarn,
    kError,
};

typedef void (*LogCallback)(LogLevel level, const char* message);

void Log(LogLevel level, const char* format, ...);
void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();
void SetLogCallback(LogCallback callback);

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_LOG_H
