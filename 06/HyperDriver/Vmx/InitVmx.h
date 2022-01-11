#pragma once


// Inizializza VMX, VMCS e lancia il guest
BOOLEAN InitializeVmx(UINT64 LogicalProcessors);
BOOLEAN VirtualizeCurrentCpu(ULONG ProcessorIndex, PVOID GuestStack);

// Alloca memoria per lo Stack dell'hypervisor durante VM Exit
BOOLEAN AllocateVmmStack(UINT64 ProcessorID);

// Alloca memoria per VMCS e VMXON region
BOOLEAN AllocateVMCSRegion(PVCPU CurrentGuestState, BOOLEAN VmxOn);

// Alloca memoria per la MSR Bitmap
BOOLEAN AllocateMsrBitmap(INT ProcessorID);