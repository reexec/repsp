#ifndef REPSP_HLE_KERNEL_INTERNAL_H
#define REPSP_HLE_KERNEL_INTERNAL_H

#include "repsp/hle/kernel.h"

namespace repsp::hle::kernel::detail
{

void RegisterSysMem();
void RegisterThreadManThread();
void RegisterThreadManSync();
void RegisterThreadManVpl();
void RegisterIoFileMgr();
void RegisterModuleMgr(uint32_t main_module_base, uint32_t main_module_size);
void RegisterLoadExec();
void RegisterUtils();
void RegisterStdio();
void RegisterKernelLibrary();
void RegisterPowerSuspend();
void RegisterUmd();
void RegisterImpose();

void ResetIoFiles();
void ResetThreadManSync();
void ResetModules();
std::vector<OpenFileInfo> SnapshotIoFiles();

} // namespace repsp::hle::kernel::detail

#endif // REPSP_HLE_KERNEL_INTERNAL_H
