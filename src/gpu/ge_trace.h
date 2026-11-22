#pragma once

#include "repsp/gpu/ge_interpreter.h"
#include "repsp/gpu/render_backend.h"

#include <cstdint>

namespace repsp::gpu::detail
{

bool GeTraceIsEnabled();
void GeTraceReloadFromEnv();
void GeTraceConfigure(const char* dir, uint64_t first_frame, uint64_t last_frame);
void GeTraceSetFrame(uint64_t frame);

void GeTraceRecordDraw(uint64_t list_num, const char* prim_name, uint32_t vtype,
                       const Vertex* verts, uint32_t count,
                       const DrawState& ds, const TransformState& xf,
                       uint32_t clut_loaded_bytes);

} // namespace repsp::gpu::detail
