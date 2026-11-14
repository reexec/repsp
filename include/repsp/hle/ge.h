#ifndef REPSP_HLE_GE_H
#define REPSP_HLE_GE_H

#include "repsp/gpu/ge_interpreter.h"

#include <string>

namespace repsp::hle::ge
{

void Register();
void Reset();

const gpu::GeStats& GetStats();
std::string FormatStats();

} // namespace repsp::hle::ge

#endif // REPSP_HLE_GE_H
