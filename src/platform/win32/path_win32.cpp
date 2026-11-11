#include "repsp/platform/path.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <vector>

namespace repsp::platform
{

std::filesystem::path GetExecutableDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }

    if (length == 0)
    {
        return std::filesystem::current_path();
    }

    std::filesystem::path exe_path(buffer.data());
    return exe_path.parent_path();
}

std::filesystem::path GetUserDataDirectory(const std::string& app_name)
{
    PWSTR local_app_data = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data)))
    {
        std::filesystem::path base(local_app_data);
        CoTaskMemFree(local_app_data);
        return base / app_name;
    }

    return GetExecutableDirectory() / "data" / app_name;
}

} // namespace repsp::platform
