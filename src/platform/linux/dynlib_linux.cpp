#include "repsp/platform/dynlib.h"

#include <dlfcn.h>

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

    dlerror();
    void* mod = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (mod == nullptr)
    {
        if (error_out != nullptr)
        {
            const char* msg = dlerror();
            *error_out = (msg != nullptr) ? msg : "dlopen failed";
        }
        return false;
    }

    handle_ = mod;
    return true;
}

void DynamicLibrary::Close()
{
    if (handle_ != nullptr)
    {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

void* DynamicLibrary::GetSymbol(const char* name) const
{
    if (handle_ == nullptr || name == nullptr)
    {
        return nullptr;
    }
    return dlsym(handle_, name);
}

const char* DynamicLibrary::SharedLibraryPrefix()
{
    return "lib";
}

const char* DynamicLibrary::SharedLibraryExtension()
{
    return ".so";
}

} // namespace repsp::platform
