#include "repsp/platform/path.h"

#include <cstdlib>
#include <unistd.h>
#include <vector>

namespace repsp::platform
{

std::filesystem::path GetExecutableDirectory()
{
    std::vector<char> buffer(1024);
    ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    while (len == static_cast<ssize_t>(buffer.size() - 1))
    {
        buffer.resize(buffer.size() * 2);
        len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    }

    if (len <= 0)
    {
        return std::filesystem::current_path();
    }

    buffer[static_cast<size_t>(len)] = '\0';
    std::filesystem::path exe_path(buffer.data());
    return exe_path.parent_path();
}

std::filesystem::path GetUserDataDirectory(const std::string& app_name)
{
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home != nullptr && *xdg_data_home != '\0')
    {
        return std::filesystem::path(xdg_data_home) / app_name;
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
        return std::filesystem::path(home) / ".local" / "share" / app_name;
    }

    return GetExecutableDirectory() / "data" / app_name;
}

} // namespace repsp::platform
