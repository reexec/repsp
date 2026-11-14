#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace repsp::hle::kernel::detail
{

namespace
{

constexpr uint32_t kPspORdOnly = 0x0001u;
constexpr uint32_t kPspOWrOnly = 0x0002u;
constexpr uint32_t kPspORdWr = 0x0003u;
constexpr uint32_t kPspOAppend = 0x0100u;
constexpr uint32_t kPspOCreat = 0x0200u;
constexpr uint32_t kPspOTrunc = 0x0400u;
constexpr uint32_t kPspOExcl = 0x0800u;

constexpr int32_t kPspSeekSet = 0;
constexpr int32_t kPspSeekCur = 1;
constexpr int32_t kPspSeekEnd = 2;

struct IoFileEntry
{
    int32_t fd = -1;
    std::FILE* fp = nullptr;
    std::string guest_path;
    uint32_t flags = 0;
    uint64_t offset = 0;
    bool has_async_op = false;
    int64_t async_result = 0;
};

std::mutex& IoMutex()
{
    static std::mutex m;
    return m;
}

std::map<int32_t, std::shared_ptr<IoFileEntry>>& IoFiles()
{
    static std::map<int32_t, std::shared_ptr<IoFileEntry>> files;
    return files;
}

int32_t& NextFd()
{
    static int32_t fd = 3;
    return fd;
}

std::string ReadGuestCString(uint32_t addr, size_t max_len = 512)
{
    if (addr == 0)
    {
        return "";
    }
    const char* p = reinterpret_cast<const char*>(runtime::GetGuestMemory().Translate(addr));
    std::string out;
    for (size_t i = 0; i < max_len && p[i] != '\0'; ++i)
    {
        out.push_back(p[i]);
    }
    return out;
}

int32_t MapVfsError(runtime::VfsResolveStatus st)
{
    switch (st)
    {
    case runtime::VfsResolveStatus::kReadOnly:
        return kSceKernelErrorErrnoEacces;
    case runtime::VfsResolveStatus::kInvalidPath:
    case runtime::VfsResolveStatus::kNoSuchDevice:
    case runtime::VfsResolveStatus::kEscapesRoot:
    case runtime::VfsResolveStatus::kNotFound:
    default:
        return kSceKernelErrorErrnoEnoent;
    }
}

std::FILE* OpenHostFile(const std::filesystem::path& host_path, uint32_t flags, int32_t* out_err)
{
    const uint32_t access = flags & 0x0003u;
    const bool write_access = (access == kPspOWrOnly || access == kPspORdWr);
    const bool rdwr = (access == kPspORdWr);
    const bool creat = (flags & kPspOCreat) != 0;
    const bool trunc = (flags & kPspOTrunc) != 0;
    const bool excl = (flags & kPspOExcl) != 0;
    const bool append = (flags & kPspOAppend) != 0;

    std::error_code ec;
    const bool exists = std::filesystem::exists(host_path, ec);
    if (creat && excl && exists)
    {
        *out_err = kSceKernelErrorErrnoEexist;
        return nullptr;
    }
    if (!creat && !exists)
    {
        *out_err = kSceKernelErrorErrnoEnoent;
        return nullptr;
    }
    if (exists && std::filesystem::is_directory(host_path, ec))
    {
        *out_err = kSceKernelErrorErrnoEacces;
        return nullptr;
    }

    const char* mode = "rb";
    if (!write_access)
    {
        mode = "rb";
    }
    else if (trunc || (creat && !exists))
    {
        mode = rdwr ? "w+b" : "wb";
    }
    else if (append)
    {
        mode = rdwr ? "a+b" : "ab";
    }
    else
    {
        mode = "r+b";
    }

#ifdef _WIN32
    std::wstring wmode;
    for (const char* p = mode; *p != '\0'; ++p)
    {
        wmode.push_back(static_cast<wchar_t>(*p));
    }
    std::FILE* fp = _wfopen(host_path.c_str(), wmode.c_str());
#else
    std::FILE* fp = std::fopen(host_path.c_str(), mode);
#endif
    if (fp == nullptr)
    {
        *out_err = kSceKernelErrorErrnoEnoent;
        return nullptr;
    }
    *out_err = 0;
    return fp;
}

int32_t DoOpen(const std::string& path, uint32_t flags, std::shared_ptr<IoFileEntry>* out_entry)
{
    const uint32_t access = flags & 0x0003u;
    const bool for_write = (access == kPspOWrOnly || access == kPspORdWr) ||
                           ((flags & (kPspOCreat | kPspOTrunc)) != 0);

    std::filesystem::path host_path;
    std::string normalized;
    auto st = runtime::GetVfs().Resolve(path, for_write, &host_path, &normalized);
    if (st != runtime::VfsResolveStatus::kOk)
    {
        return MapVfsError(st);
    }

    int32_t err = 0;
    std::FILE* fp = OpenHostFile(host_path, flags, &err);
    if (fp == nullptr)
    {
        return err;
    }

    auto entry = std::make_shared<IoFileEntry>();
    entry->fp = fp;
    entry->guest_path = normalized;
    entry->flags = flags;
    entry->offset = 0;

    std::lock_guard<std::mutex> lock(IoMutex());
    while (NextFd() < 3 || IoFiles().find(NextFd()) != IoFiles().end())
    {
        NextFd() = (NextFd() >= 0x7FFFFFFF) ? 3 : (NextFd() + 1);
    }
    entry->fd = NextFd()++;
    IoFiles()[entry->fd] = entry;
    if (out_entry != nullptr)
    {
        *out_entry = entry;
    }

    if (runtime::GetNidRegistry().IsTraceEnabled())
    {
        runtime::Log(runtime::LogLevel::kInfo, "[IO] open(\"%s\", flags=0x%X) -> fd=%d", normalized.c_str(), flags, entry->fd);
    }
    return entry->fd;
}

int32_t DoRead(const std::shared_ptr<IoFileEntry>& entry, uint32_t data_addr, uint32_t size)
{
    if (size == 0)
    {
        return 0;
    }
    if (data_addr == 0)
    {
        return kSceKernelErrorIllegalAddr;
    }
    uint8_t* dst = runtime::GetGuestMemory().Translate(data_addr);
    size_t read_bytes = std::fread(dst, 1, size, entry->fp);
    entry->offset += read_bytes;
    if (runtime::GetNidRegistry().IsTraceEnabled())
    {
        runtime::Log(runtime::LogLevel::kInfo, "[IO] read(fd=%d \"%s\", size=%u) -> %zu", entry->fd, entry->guest_path.c_str(), size, read_bytes);
    }
    return static_cast<int32_t>(read_bytes);
}

int32_t DoWrite(int32_t fd, uint32_t data_addr, uint32_t size)
{
    if (data_addr == 0 && size > 0)
    {
        return kSceKernelErrorIllegalAddr;
    }
    const uint8_t* src = (size > 0) ? runtime::GetGuestMemory().Translate(data_addr) : nullptr;

    if (fd == 1 || fd == 2)
    {
        if (size > 0)
        {
            std::string msg(reinterpret_cast<const char*>(src), size);
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
            {
                msg.pop_back();
            }
            runtime::Log(runtime::LogLevel::kInfo, "[guest stdout] %s", msg.c_str());
        }
        return static_cast<int32_t>(size);
    }

    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            return kSceKernelErrorBadf;
        }
        entry = it->second;
    }

    if ((entry->flags & 0x0003u) == kPspORdOnly)
    {
        return kSceKernelErrorErrnoEacces;
    }

    size_t written = std::fwrite(src, 1, size, entry->fp);
    std::fflush(entry->fp);
    entry->offset += written;
    return static_cast<int32_t>(written);
}

int64_t DoSeek(const std::shared_ptr<IoFileEntry>& entry, int64_t offset, int32_t whence)
{
    int origin = SEEK_SET;
    if (whence == kPspSeekSet) origin = SEEK_SET;
    else if (whence == kPspSeekCur) origin = SEEK_CUR;
    else if (whence == kPspSeekEnd) origin = SEEK_END;
    else return kSceKernelErrorErrnoEinval;

#ifdef _WIN32
    if (_fseeki64(entry->fp, offset, origin) != 0)
    {
        return kSceKernelErrorError;
    }
    int64_t pos = _ftelli64(entry->fp);
#else
    if (fseeko(entry->fp, static_cast<off_t>(offset), origin) != 0)
    {
        return kSceKernelErrorError;
    }
    int64_t pos = static_cast<int64_t>(ftello(entry->fp));
#endif
    if (pos < 0)
    {
        return kSceKernelErrorError;
    }
    entry->offset = static_cast<uint64_t>(pos);
    if (runtime::GetNidRegistry().IsTraceEnabled())
    {
        runtime::Log(runtime::LogLevel::kInfo, "[IO] lseek(fd=%d \"%s\", offset=%lld, whence=%d) -> %lld",
                     entry->fd, entry->guest_path.c_str(), static_cast<long long>(offset), whence, static_cast<long long>(pos));
    }
    return pos;
}

void HleIoOpen()
{
    uint32_t path_addr = recomp::Arg<uint32_t>(0);
    uint32_t flags = recomp::Arg<uint32_t>(1);
    // arg 2: mode

    std::string path = ReadGuestCString(path_addr);
    int32_t res = DoOpen(path, flags, nullptr);
    recomp::Return(res);
}

void HleIoClose()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            recomp::Return(kSceKernelErrorBadf);
            return;
        }
        entry = it->second;
        IoFiles().erase(it);
    }

    if (entry->fp != nullptr)
    {
        std::fclose(entry->fp);
        entry->fp = nullptr;
    }
    if (runtime::GetNidRegistry().IsTraceEnabled())
    {
        runtime::Log(runtime::LogLevel::kInfo, "[IO] close(fd=%d \"%s\")", fd, entry->guest_path.c_str());
    }
    recomp::Return(0);
}

void HleIoRead()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t data_addr = recomp::Arg<uint32_t>(1);
    uint32_t size = recomp::Arg<uint32_t>(2);

    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            recomp::Return(kSceKernelErrorBadf);
            return;
        }
        entry = it->second;
    }
    recomp::Return(DoRead(entry, data_addr, size));
}

void HleIoWrite()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t data_addr = recomp::Arg<uint32_t>(1);
    uint32_t size = recomp::Arg<uint32_t>(2);
    recomp::Return(DoWrite(fd, data_addr, size));
}

void HleIoLseek()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    // MIPS o32 64-bit arg alignment: $a1 is padding, offset is in $a2:$a3, whence is in $t0 (index 4).
    uint32_t off_lo = recomp::Arg<uint32_t>(2);
    uint32_t off_hi = recomp::Arg<uint32_t>(3);
    int64_t offset = static_cast<int64_t>(static_cast<uint64_t>(off_lo) | (static_cast<uint64_t>(off_hi) << 32));
    int32_t whence = recomp::Arg<int32_t>(4);

    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            recomp::Return(static_cast<int64_t>(kSceKernelErrorBadf));
            return;
        }
        entry = it->second;
    }
    recomp::Return(DoSeek(entry, offset, whence));
}

void HleIoOpenAsync()
{
    uint32_t path_addr = recomp::Arg<uint32_t>(0);
    uint32_t flags = recomp::Arg<uint32_t>(1);

    std::string path = ReadGuestCString(path_addr);
    std::shared_ptr<IoFileEntry> entry;
    int32_t res = DoOpen(path, flags, &entry);
    if (res >= 0 && entry)
    {
        entry->has_async_op = true;
        entry->async_result = res;
    }
    recomp::Return(res);
}

void HleIoReadAsync()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t data_addr = recomp::Arg<uint32_t>(1);
    uint32_t size = recomp::Arg<uint32_t>(2);

    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            recomp::Return(kSceKernelErrorBadf);
            return;
        }
        entry = it->second;
        if (entry->has_async_op)
        {
            recomp::Return(kSceKernelErrorAsyncBusy);
            return;
        }
        entry->has_async_op = true;
    }

    entry->async_result = DoRead(entry, data_addr, size);
    recomp::Return(0);
}

void HleIoLseekAsync()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t off_lo = recomp::Arg<uint32_t>(2);
    uint32_t off_hi = recomp::Arg<uint32_t>(3);
    int64_t offset = static_cast<int64_t>(static_cast<uint64_t>(off_lo) | (static_cast<uint64_t>(off_hi) << 32));
    int32_t whence = recomp::Arg<int32_t>(4);

    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            recomp::Return(kSceKernelErrorBadf);
            return;
        }
        entry = it->second;
        if (entry->has_async_op)
        {
            recomp::Return(kSceKernelErrorAsyncBusy);
            return;
        }
        entry->has_async_op = true;
    }

    entry->async_result = DoSeek(entry, offset, whence);
    recomp::Return(0);
}

int32_t FinishAsyncWait(int32_t fd, uint32_t res_addr, bool allow_callbacks)
{
    if (allow_callbacks)
    {
        runtime::GetScheduler().CheckCallbacks();
    }

    std::shared_ptr<IoFileEntry> entry;
    {
        std::lock_guard<std::mutex> lock(IoMutex());
        auto it = IoFiles().find(fd);
        if (it == IoFiles().end())
        {
            return kSceKernelErrorBadf;
        }
        entry = it->second;
        if (!entry->has_async_op)
        {
            return kSceKernelErrorError;
        }
        if (res_addr != 0)
        {
            runtime::GetGuestMemory().WriteU64(res_addr, static_cast<uint64_t>(entry->async_result));
        }
        entry->has_async_op = false;
    }

    return 0;
}

void HleIoWaitAsync()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t res_addr = recomp::Arg<uint32_t>(1);
    recomp::Return(FinishAsyncWait(fd, res_addr, false));
}

void HleIoWaitAsyncCB()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t res_addr = recomp::Arg<uint32_t>(1);
    recomp::Return(FinishAsyncWait(fd, res_addr, true));
}

void HleIoPollAsync()
{
    int32_t fd = recomp::Arg<int32_t>(0);
    uint32_t res_addr = recomp::Arg<uint32_t>(1);

    std::lock_guard<std::mutex> lock(IoMutex());
    auto it = IoFiles().find(fd);
    if (it == IoFiles().end())
    {
        recomp::Return(kSceKernelErrorBadf);
        return;
    }
    auto entry = it->second;
    if (!entry->has_async_op)
    {
        recomp::Return(1);
        return;
    }
    if (res_addr != 0)
    {
        runtime::GetGuestMemory().WriteU64(res_addr, static_cast<uint64_t>(entry->async_result));
    }
    entry->has_async_op = false;
    recomp::Return(0);
}

} // namespace

void ResetIoFiles()
{
    std::lock_guard<std::mutex> lock(IoMutex());
    for (auto& kv : IoFiles())
    {
        if (kv.second->fp != nullptr)
        {
            std::fclose(kv.second->fp);
            kv.second->fp = nullptr;
        }
    }
    IoFiles().clear();
    NextFd() = 3;
}

std::vector<OpenFileInfo> SnapshotIoFiles()
{
    std::lock_guard<std::mutex> lock(IoMutex());
    std::vector<OpenFileInfo> out;
    out.reserve(IoFiles().size());
    for (const auto& kv : IoFiles())
    {
        OpenFileInfo info;
        info.fd = kv.second->fd;
        info.guest_path = kv.second->guest_path;
        info.offset = kv.second->offset;
        info.has_async_result = kv.second->has_async_op;
        out.push_back(std::move(info));
    }
    return out;
}

void RegisterIoFileMgr()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("IoFileMgrForUser", "sceIoOpen", HleIoOpen);
    reg.RegisterByName("IoFileMgrForUser", "sceIoClose", HleIoClose);
    reg.RegisterByName("IoFileMgrForUser", "sceIoRead", HleIoRead);
    reg.RegisterByName("IoFileMgrForUser", "sceIoWrite", HleIoWrite);
    reg.RegisterByName("IoFileMgrForUser", "sceIoLseek", HleIoLseek);
    reg.RegisterByName("IoFileMgrForUser", "sceIoOpenAsync", HleIoOpenAsync);
    reg.RegisterByName("IoFileMgrForUser", "sceIoReadAsync", HleIoReadAsync);
    reg.RegisterByName("IoFileMgrForUser", "sceIoLseekAsync", HleIoLseekAsync);
    reg.RegisterByName("IoFileMgrForUser", "sceIoWaitAsync", HleIoWaitAsync);
    reg.RegisterByName("IoFileMgrForUser", "sceIoWaitAsyncCB", HleIoWaitAsyncCB);
    reg.RegisterByName("IoFileMgrForUser", "sceIoPollAsync", HleIoPollAsync);
}

} // namespace repsp::hle::kernel::detail
