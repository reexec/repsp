#include "repsp/codec/loader.h"
#include "repsp/platform/dynlib.h"
#include "repsp/platform/path.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
    using namespace repsp::codec;
    using namespace repsp::platform;

    const std::filesystem::path default_name = DefaultCodecPluginFilename();
    if (default_name.empty())
    {
        std::fprintf(stderr, "DefaultCodecPluginFilename returned empty\n");
        return 1;
    }

    // 1. Missing path must fail cleanly.
    std::string err;
    auto missing = LoadCodecPlugin(GetExecutableDirectory() / "nonexistent_codec_plugin_12345.dll", &err);
    if (missing != nullptr || err.empty())
    {
        std::fprintf(stderr, "Expected missing plugin load to fail with error\n");
        return 1;
    }

    // 2. Non-plugin file must fail cleanly.
    const std::filesystem::path bad_file = GetExecutableDirectory() / "codec_loader_probe_not_a_dll.txt";
    {
        std::ofstream ofs(bad_file, std::ios::binary | std::ios::trunc);
        ofs << "not a shared library";
    }
    err.clear();
    auto bad_lib = LoadCodecPlugin(bad_file, &err);
    std::error_code ec;
    std::filesystem::remove(bad_file, ec);
    if (bad_lib != nullptr || err.empty())
    {
        std::fprintf(stderr, "Expected non-library load to fail with error\n");
        return 1;
    }

    // 3. Fake plugin with mismatched ABI version must be rejected.
    std::string fake_name = DynamicLibrary::SharedLibraryPrefix();
    fake_name += "codec_fake_bad_abi_plugin";
    fake_name += DynamicLibrary::SharedLibraryExtension();
    const std::filesystem::path fake_path = GetExecutableDirectory() / fake_name;
    err.clear();
    auto bad_abi = LoadCodecPlugin(fake_path, &err);
    if (bad_abi != nullptr || err.find("ABI") == std::string::npos)
    {
        std::fprintf(stderr, "Expected ABI mismatch rejection, got err='%s'\n", err.c_str());
        return 1;
    }

    std::printf("codec_loader_probe: PASS\n");
    return 0;
}
