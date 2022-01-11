#pragma once

// Gestori delle varie VM exit
VOID VmxRootExitHandleMsrRead(PGUEST_REGS GuestRegs);
VOID VmxRootExitHandleMsrWrite(PGUEST_REGS GuestRegs);
VOID VmxRootExitHandleCpuid(PGUEST_REGS RegistersState);
NTSTATUS VmxRootVmcallHandler(UINT64 VmcallNumber, UINT64 OptionalParam1, UINT64 OptionalParam2, UINT64 OptionalParam3);