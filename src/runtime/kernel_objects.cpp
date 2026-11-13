#include "repsp/runtime/kernel_objects.h"

namespace repsp::runtime
{

KernelUid KernelObjectTable::Create(KernelObjectType type, std::shared_ptr<void> object)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Allocate positive 32-bit UID, skipping wraparound to <= 0 or already-in-use IDs.
    while (next_uid_ <= 0 || entries_.find(next_uid_) != entries_.end())
    {
        next_uid_ = (next_uid_ >= 0x7FFFFFFF) ? 1 : (next_uid_ + 1);
    }
    KernelUid uid = next_uid_++;
    entries_[uid] = Entry{ type, std::move(object) };
    return uid;
}

LookupStatus KernelObjectTable::Lookup(KernelUid uid, KernelObjectType expected_type, std::shared_ptr<void>* out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(uid);
    if (it == entries_.end())
    {
        return LookupStatus::kNotFound;
    }
    if (it->second.type != expected_type)
    {
        return LookupStatus::kWrongType;
    }
    if (out != nullptr)
    {
        *out = it->second.object;
    }
    return LookupStatus::kOk;
}

LookupStatus KernelObjectTable::Remove(KernelUid uid, KernelObjectType expected_type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(uid);
    if (it == entries_.end())
    {
        return LookupStatus::kNotFound;
    }
    if (it->second.type != expected_type)
    {
        return LookupStatus::kWrongType;
    }
    entries_.erase(it);
    return LookupStatus::kOk;
}

void KernelObjectTable::ForEach(KernelObjectType type, const std::function<void(KernelUid, const std::shared_ptr<void>&)>& fn) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : entries_)
    {
        if (kv.second.type == type)
        {
            fn(kv.first, kv.second.object);
        }
    }
}

void KernelObjectTable::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    next_uid_ = 1;
}

KernelObjectTable& GetKernelObjects()
{
    static KernelObjectTable instance;
    return instance;
}

} // namespace repsp::runtime
