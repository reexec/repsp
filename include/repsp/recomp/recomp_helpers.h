#ifndef REPSP_RECOMP_RECOMP_HELPERS_H
#define REPSP_RECOMP_RECOMP_HELPERS_H

#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"

#include <cstdint>
#include <type_traits>

namespace repsp::recomp
{

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

inline void Return()
{
}

} // namespace repsp::recomp

#endif // REPSP_RECOMP_RECOMP_HELPERS_H
