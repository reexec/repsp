#ifndef REPSP_RUNTIME_CLOCK_H
#define REPSP_RUNTIME_CLOCK_H

#include <cstdint>

namespace repsp::runtime
{

uint64_t GetSystemTimeWide();
uint32_t GetSystemTimeLow();
void ResetBootClock();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_CLOCK_H
