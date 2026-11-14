#ifndef REPSP_RUNTIME_VFS_H
#define REPSP_RUNTIME_VFS_H

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace repsp::runtime
{

enum class VfsResolveStatus
{
    kOk = 0,
    kInvalidPath,
    kNoSuchDevice,
    kEscapesRoot,
    kReadOnly,
    kNotFound,
};

struct VfsMount
{
    std::filesystem::path host_root;
    bool read_only = false;
};

class Vfs
{
public:
    void Mount(const std::string& device, const std::filesystem::path& host_root, bool read_only);
    void Unmount(const std::string& device);
    void Clear();

    VfsResolveStatus Resolve(const std::string& guest_path, bool for_write, std::filesystem::path* out_host_path, std::string* out_normalized_guest_path = nullptr) const;

private:
    struct MountEntry
    {
        std::filesystem::path host_root;
        bool read_only = false;
    };

    static std::string NormalizeDeviceName(const std::string& device);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, MountEntry> mounts_;
};

Vfs& GetVfs();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_VFS_H
