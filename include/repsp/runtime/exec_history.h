#ifndef REPSP_RUNTIME_EXEC_HISTORY_H
#define REPSP_RUNTIME_EXEC_HISTORY_H

#include <cstdint>

namespace repsp::runtime
{

void NoteGuestEntry(uint32_t addr);

void NoteHleCallBegin(uint32_t nid, const char* name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
void NoteHleCallEnd(uint32_t ret);

void SetCurrentThreadExecInfo(uint64_t host_tid, int32_t guest_uid, const char* name, int32_t priority);
void ClearCurrentThreadExecInfo();

void SetCrashStopReason(const char* reason);
void SetCrashSchedulerMutexOwner(uint64_t host_tid, int32_t guest_uid);
// Records, once, the thread found holding the scheduler mutex by boundary
// recovery. Unlike the live owner, this does not change after the release.
void SetCrashSchedulerMutexRecovery(uint64_t host_tid, int32_t guest_uid);

void ClearCrashThreadSnapshots();
void UpdateCrashThreadSnapshot(
    int32_t uid,
    const char* name,
    int32_t priority,
    const char* state_str,
    const char* wait_type_str,
    int32_t wait_target_uid,
    const char* wait_detail);

void RegisterRuntimeCrashProvider();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_EXEC_HISTORY_H
