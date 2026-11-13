#ifndef REPSP_RUNTIME_KERNEL_OBJECTS_H
#define REPSP_RUNTIME_KERNEL_OBJECTS_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace repsp::runtime
{

using KernelUid = int32_t;

enum class KernelObjectType
{
    kUnknown = 0,
    kThread,
    kSema,
    kEventFlag,
    kCallback,
    kPartitionBlock,
    kFile,
    kModule,
};

enum class LookupStatus
{
    kOk = 0,
    kNotFound,
    kWrongType,
};

class KernelObjectTable
{
public:
    KernelUid Create(KernelObjectType type, std::shared_ptr<void> object);

    LookupStatus Lookup(KernelUid uid, KernelObjectType expected_type, std::shared_ptr<void>* out) const;
    LookupStatus Remove(KernelUid uid, KernelObjectType expected_type);

    template<typename T>
    LookupStatus LookupAs(KernelUid uid, KernelObjectType expected_type, std::shared_ptr<T>* out) const
    {
        std::shared_ptr<void> raw;
        LookupStatus st = Lookup(uid, expected_type, &raw);
        if (st == LookupStatus::kOk && out != nullptr)
        {
            *out = std::static_pointer_cast<T>(raw);
        }
        return st;
    }

    void ForEach(KernelObjectType type, const std::function<void(KernelUid, const std::shared_ptr<void>&)>& fn) const;
    void Reset();

private:
    struct Entry
    {
        KernelObjectType type = KernelObjectType::kUnknown;
        std::shared_ptr<void> object;
    };

    mutable std::mutex mutex_;
    KernelUid next_uid_ = 1;
    std::unordered_map<KernelUid, Entry> entries_;
};

KernelObjectTable& GetKernelObjects();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_KERNEL_OBJECTS_H
