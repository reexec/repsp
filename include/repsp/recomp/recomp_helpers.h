#ifndef REPSP_RECOMP_RECOMP_HELPERS_H
#define REPSP_RECOMP_RECOMP_HELPERS_H

#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace repsp::recomp
{

using GuestAddress = uint32_t;

inline uint32_t GetGpr(int index)
{
    const auto* ops = repsp_recomp_get_cpu_ops();
    return (ops != nullptr && ops->get_gpr != nullptr) ? ops->get_gpr(index) : 0;
}

inline void SetGpr(int index, uint32_t value)
{
    const auto* ops = repsp_recomp_get_cpu_ops();
    if (ops != nullptr && ops->set_gpr != nullptr)
    {
        ops->set_gpr(index, value);
    }
}

inline uint32_t GetFpr(int index)
{
    const auto* ops = repsp_recomp_get_cpu_ops();
    return (ops != nullptr && ops->get_fpr != nullptr) ? ops->get_fpr(index) : 0;
}

inline void SetFpr(int index, uint32_t value)
{
    const auto* ops = repsp_recomp_get_cpu_ops();
    if (ops != nullptr && ops->set_fpr != nullptr)
    {
        ops->set_fpr(index, value);
    }
}

// Inferred: Arguments beyond 8 are passed on stack at $sp + (index - 8) * 4.
// To be confirmed when implementing the first firmware function with > 8 arguments.
inline uint32_t ReadRawArg(size_t index)
{
    if (index < 4)
    {
        return GetGpr(static_cast<int>(4 + index)); // $a0..$a3
    }
    if (index < 8)
    {
        return GetGpr(static_cast<int>(8 + (index - 4))); // $t0..$t3
    }
    uint32_t sp = GetGpr(29); // $sp
    uint32_t arg_addr = sp + static_cast<uint32_t>((index - 8) * 4);
    return runtime::GetGuestMemory().ReadU32(arg_addr);
}

template<typename T = uint32_t>
inline T Arg(size_t index)
{
    uint32_t raw = ReadRawArg(index);
    if constexpr (std::is_pointer_v<T>)
    {
        using NonPtr = std::remove_pointer_t<T>;
        using Decayed = std::decay_t<NonPtr>;
        static_assert(
            std::is_same_v<Decayed, uint8_t> ||
            std::is_same_v<Decayed, char>,
            "Arg<T*> allows only uint8_t*, const uint8_t*, char*, const char*. "
            "Structures must be passed by GuestAddress and read through GuestMemory."
        );

        if (raw == 0)
        {
            return nullptr;
        }
        return reinterpret_cast<T>(runtime::GetGuestMemory().Translate(raw));
    }
    else
    {
        return static_cast<T>(raw);
    }
}

inline void Return(uint32_t value)
{
    SetGpr(2, value); // $v0
}

inline void Return(int32_t value)
{
    SetGpr(2, static_cast<uint32_t>(value));
}

inline void Return(uint64_t value)
{
    SetGpr(2, static_cast<uint32_t>(value & 0xFFFFFFFF));
    SetGpr(3, static_cast<uint32_t>(value >> 32));
}

inline void Return(int64_t value)
{
    Return(static_cast<uint64_t>(value));
}

inline void Return(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    SetFpr(0, bits); // $f0
}

inline void Return()
{
}

} // namespace repsp::recomp

#endif // REPSP_RECOMP_RECOMP_HELPERS_H
