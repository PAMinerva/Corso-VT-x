#pragma once

#define POOLTAG             0x56542d78 // VT-x
#define VMM_STACK_SIZE      0x5000     // Kernel stack è 16 o 20 KB

// Alloca memoria per lo Stack dell'hypervisor durante VM Exit
BOOLEAN AllocateVmmStack(UINT64 ProcessorID);

// Alloca memoria per VMCS e VMXON region
BOOLEAN AllocateVMCSRegion(PVCPU vms, BOOLEAN vmxon);