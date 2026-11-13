#include "repsp/runtime/vfs.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace repsp::runtime
{

namespace
{

std::string ToLowerAscii(const std::string& s)
{
    std::string out = s;
    for (char& ch : out)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::string Vfs::NormalizeDeviceName(const std::string& device)
{
    std::string name = ToLowerAscii(device);
    while (!name.empty() && name.back() == ':')
    {
        name.pop_back();
    }
    return name;
}

void Vfs::Mount(const std::string& device, const std::filesystem::path& host_root, bool read_only)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = NormalizeDeviceName(device);
    if (!key.empty())
    {
        mounts_[key] = MountEntry{ host_root, read_only };
    }
}

void Vfs::Unmount(const std::string& device)
{
    std::lock_guard<std::mutex> lock(mutex_);
    mounts_.erase(NormalizeDeviceName(device));
}

void Vfs::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    mounts_.clear();
}

VfsResolveStatus Vfs::Resolve(const std::string& guest_path, bool for_write, std::filesystem::path* out_host_path, std::string* out_normalized_guest_path) const
{
    if (guest_path.empty())
    {
        return VfsResolveStatus::kInvalidPath;
    }

    const size_t colon = guest_path.find(':');
    if (colon == std::string::npos || colon == 0)
    {
        return VfsResolveStatus::kInvalidPath;
    }

    const std::string device_key = NormalizeDeviceName(guest_path.substr(0, colon));
    const std::string raw_rest = guest_path.substr(colon + 1);

    MountEntry mount;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mounts_.find(device_key);
        if (it == mounts_.end())
        {
            return VfsResolveStatus::kNoSuchDevice;
        }
        mount = it->second;
    }

    if (for_write && mount.read_only)
    {
        return VfsResolveStatus::kReadOnly;
    }

    std::vector<std::string> parts;
    std::string current_token;
    for (char ch : raw_rest)
    {
        if (ch == '/' || ch == '\\')
        {
            if (!current_token.empty())
            {
                if (current_token == "..")
                {
                    if (parts.empty())
                    {
                        return VfsResolveStatus::kEscapesRoot;
                    }
                    parts.pop_back();
                }
                else if (current_token != ".")
                {
                    parts.push_back(current_token);
                }
                current_token.clear();
            }
        }
        else
        {
            current_token.push_back(ch);
        }
    }
    if (!current_token.empty())
    {
        if (current_token == "..")
        {
            if (parts.empty())
            {
                return VfsResolveStatus::kEscapesRoot;
            }
            parts.pop_back();
        }
        else if (current_token != ".")
        {
            parts.push_back(current_token);
        }
    }

    if (out_normalized_guest_path != nullptr)
    {
        std::string norm = device_key + ":/";
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
            {
                norm.push_back('/');
            }
            norm += parts[i];
        }
        *out_normalized_guest_path = std::move(norm);
    }

    std::filesystem::path resolved = mount.host_root;
    std::error_code ec;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const std::string& comp = parts[i];
        const bool is_last = (i + 1 == parts.size());

        std::filesystem::path direct = resolved / comp;
        if (std::filesystem::exists(direct, ec))
        {
            resolved = direct;
            continue;
        }

        // Case-insensitive component lookup for case-sensitive host filesystems.
        bool matched = false;
        if (std::filesystem::exists(resolved, ec) && std::filesystem::is_directory(resolved, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(resolved, ec))
            {
                std::string fname = entry.path().filename().string();
                if (EqualsIgnoreCase(fname, comp))
                {
                    resolved = entry.path();
                    matched = true;
                    break;
                }
            }
        }

        if (!matched)
        {
            if (is_last && for_write)
            {
                resolved /= comp;
                break;
            }
            return VfsResolveStatus::kNotFound;
        }
    }

    if (out_host_path != nullptr)
    {
        *out_host_path = resolved;
    }
    return VfsResolveStatus::kOk;
}

Vfs& GetVfs()
{
    static Vfs instance;
    return instance;
}

} // namespace repsp::runtime
