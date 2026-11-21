#ifndef REPSP_RUNTIME_PRX_LOADER_H
#define REPSP_RUNTIME_PRX_LOADER_H

#include <optional>
#include <span>
#include <string>
#include <repsp/module/prx.h>
#include <repsp/runtime/guest_memory.h>

namespace repsp::runtime
{

// Copy segments of a PSP PRX (0xFFA0) or fixed-address ET_EXEC (2) to guest memory
// at base, zero BSS, and apply PRX relocations when applicable.
std::optional<module::LoadedPrx> LoadPrx(std::span<const uint8_t> file, uint32_t base,
                                         GuestMemory& memory, std::string* error = nullptr);

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_PRX_LOADER_H
