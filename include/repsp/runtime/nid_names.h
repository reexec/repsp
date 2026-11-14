// Public PSP SDK function name -> NID table, computed via SHA-1 at startup.
// Sources: PSPSDK public headers (MIT/BSD, https://github.com/pspdev/pspsdk):
//   pspthreadman.h, pspkernel.h, pspiofilemgr.h, pspmodulemgr.h, psputils.h,
//   psploadexec.h, psppower.h, pspumd.h, pspge.h, pspdisplay.h, pspctrl.h,
//   pspaudio.h, psputility.h, psprtc.h, pspmpeg.h, pspaudiocodec.h.

#ifndef REPSP_RUNTIME_NID_NAMES_H
#define REPSP_RUNTIME_NID_NAMES_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace repsp::runtime
{

struct NidNameEntry
{
    uint32_t nid = 0;
    std::string library;
    std::string name;
    bool collision = false;
};

// Returns the full SHA-1-computed NID -> name table (built once on first call).
// Collisions (two names hashing to the same 32-bit NID) are logged and combined.
const std::unordered_map<uint32_t, NidNameEntry>& GetKnownNidTable();

// Look up a known NID. Returns nullptr if not in the table.
const NidNameEntry* LookupKnownNid(uint32_t nid);

// Format as "library:name (0xNNNNNNNN)" if known, or "0xNNNNNNNN" if unknown.
std::string FormatKnownNid(uint32_t nid);

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_NID_NAMES_H
