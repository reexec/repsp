#ifndef REPSP_PLATFORM_DYNLIB_H
#define REPSP_PLATFORM_DYNLIB_H

#include <cstring>
#include <filesystem>
#include <string>

namespace repsp::platform
{

class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    bool Open(const std::filesystem::path& path, std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const { return handle_ != nullptr; }

    void* GetSymbol(const char* name) const;

    template <typename Fn>
    Fn GetSymbolAs(const char* name) const
    {
        void* sym = GetSymbol(name);
        if (sym == nullptr)
        {
            return nullptr;
        }
        Fn fn = nullptr;
        static_assert(sizeof(fn) == sizeof(sym), "Function pointer size mismatch");
        std::memcpy(&fn, &sym, sizeof(fn));
        return fn;
    }

    static const char* SharedLibraryPrefix();
    static const char* SharedLibraryExtension();

private:
    void* handle_ = nullptr;
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_DYNLIB_H
