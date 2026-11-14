#include "repsp/platform/dynlib.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>

namespace repsp::platform
{

DynamicLibrary::~DynamicLibrary()
{
    Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_)
{
    other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool DynamicLibrary::Open(const std::filesystem::path& path, std::string* error_out)
{
    Close();
    if (path.empty())
    {
        if (error_out != nullptr)
        {
            *error_out = "empty library path";
        }
        return false;
    }

    DWORD prev_mode = 0;
    const BOOL mode_set = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &prev_mode);
    HMODULE mod = LoadLibraryW(path.c_str());
    const DWORD err = (mod == nullptr) ? GetLastError() : 0;
    if (mode_set)
    {
        SetThreadErrorMode(prev_mode, nullptr);
    }
    if (mod == nullptr)
    {
        if (error_out != nullptr)
        {
            *error_out = "LoadLibraryW failed (error " + std::to_string(err) + ")";
        }
        return false;
    }

    handle_ = static_cast<void*>(mod);
    return true;
}

void DynamicLibrary::Close()
{
    if (handle_ != nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(handle_));
        handle_ = nullptr;
    }
}

void* DynamicLibrary::GetSymbol(const char* name) const
{
    if (handle_ == nullptr || name == nullptr)
    {
        return nullptr;
    }
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(handle_), name);
    if (proc == nullptr)
    {
        return nullptr;
    }
    void* result = nullptr;
    static_assert(sizeof(result) == sizeof(proc), "Pointer size mismatch");
    std::memcpy(&result, &proc, sizeof(result));
    return result;
}

const char* DynamicLibrary::SharedLibraryPrefix()
{
    return "";
}

const char* DynamicLibrary::SharedLibraryExtension()
{
    return ".dll";
}

} // namespace repsp::platform
