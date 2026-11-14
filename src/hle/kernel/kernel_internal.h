#ifndef REPSP_HLE_KERNEL_INTERNAL_H
#define REPSP_HLE_KERNEL_INTERNAL_H

#include "repsp/hle/kernel.h"

namespace repsp::hle::kernel::detail
{

void RegisterSysMem();
void RegisterThreadManThread();
void RegisterThreadManSync();
void RegisterIoFileMgr();
void RegisterModuleMgr();
void RegisterLoadExec();
void RegisterUtils();
void RegisterStdio();
void RegisterKernelLibrary();
void RegisterPowerSuspend();
void RegisterUmd();

void ResetIoFiles();
void ResetThreadManSync();
void ResetModules();
std::vector<OpenFileInfo> SnapshotIoFiles();

} // namespace repsp::hle::kernel::detail

#endif // REPSP_HLE_KERNEL_INTERNAL_H
