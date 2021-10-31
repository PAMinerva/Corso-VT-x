#pragma once

#define POOLTAG             0x56542d78 // VT-x
#define VMM_STACK_SIZE      0x5000     // Kernel stack è 16 o 20 KB

BOOLEAN AllocateVmmStack(UINT64 ProcessorID);
BOOLEAN AllocateVMCSRegion(PVCPU vms, BOOLEAN vmxon);