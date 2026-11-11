#ifndef REPSP_PLATFORM_PATH_H
#define REPSP_PLATFORM_PATH_H

#include <filesystem>
#include <string>

namespace repsp::platform
{

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetUserDataDirectory(const std::string& app_name);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_PATH_H
