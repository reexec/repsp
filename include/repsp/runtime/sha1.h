#ifndef REPSP_RUNTIME_SHA1_H
#define REPSP_RUNTIME_SHA1_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace repsp::runtime
{

void ComputeSha1(const uint8_t* data, size_t length, uint8_t digest[20]);
uint32_t ComputeNid(const std::string& function_name);

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_SHA1_H
