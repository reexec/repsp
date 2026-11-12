#include <repsp/runtime/prx_loader.h>

#include <iomanip>
#include <sstream>

namespace repsp::runtime
{

std::optional<module::LoadedPrx> LoadPrx(std::span<const uint8_t> file, uint32_t base,
                                         GuestMemory& memory, std::string* error)
{
    auto loaded = module::ParsePrx(file, base, error);
    if (!loaded)
    {
        return std::nullopt;
    }

    auto buffer = module::RelocateToBuffer(file, base, error);
    if (!buffer)
    {
        return std::nullopt;
    }

    size_t size = buffer->size();
    if (!memory.IsValidAddress(base, size))
    {
        if (error != nullptr)
        {
            std::ostringstream oss;
            oss << "Target memory range [0x" << std::hex << std::uppercase << base
                << ", 0x" << (base + size) << ") is outside valid committed guest memory";
            *error = oss.str();
        }
        return std::nullopt;
    }

    memory.WriteBlock(base, buffer->data(), size);
    return loaded;
}

} // namespace repsp::runtime
