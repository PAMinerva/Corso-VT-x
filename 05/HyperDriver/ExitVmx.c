#include <ntddk.h>
#include "ExitVmx.h"
#include "Globals.h"
#include "Asm\AsmOperations.h"


VOID TerminateVmx(UINT64 LogicalProcessors)
{
	PROCESSOR_NUMBER ProcessorNumber;
	GROUP_AFFINITY Affinity, OldAffinity;
	NTSTATUS Status;

	for (ULONG i = 0; i < LogicalProcessors; i++)
	{
		KeGetProcessorNumberFromIndex(i, &ProcessorNumber);

		RtlSecureZeroMemory(&Affinity, sizeof(GROUP_AFFINITY));
		Affinity.Group = ProcessorNumber.Group;
		Affinity.Mask = (KAFFINITY)((ULONG64)1 << ProcessorNumber.Number);
		KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

		// Esegue VMCALL per segnalare all'hypervisor di usare VMXOFF
		// per uscire dalla VMX operation.
		Status = AsmVmxNonRootVmcall(VMCALL_VMXOFF, 0, 0, 0);

		if (Status == STATUS_SUCCESS)
		{
			KdPrint(("[*] VMX Terminated on logical core %d\n", i));

			if (GuestState && GuestState[i].VMXON_REGION_VA)
				MmFreeContiguousMemory((PUINT64)GuestState[i].VMXON_REGION_VA);

			if (GuestState && GuestState[i].VMCS_REGION_VA)
				MmFreeContiguousMemory((PUINT64)GuestState[i].VMCS_REGION_VA);

			if (GuestState && GuestState[i].VMM_STACK)
				ExFreePoolWithTag((PUINT64)GuestState[i].VMM_STACK, POOLTAG);

			if (GuestState && GuestState[i].MSR_BITMAP_VA)
				ExFreePoolWithTag((PUINT64)GuestState[i].MSR_BITMAP_VA, POOLTAG);
		}

		KeRevertToUserGroupAffinityThread(&OldAffinity);
	}
}


UINT64 ReturnRSPForVmxoff()
{
	return GuestState[KeGetCurrentProcessorNumber()].VmxoffState.GuestRsp;
}


UINT64 ReturnRIPForVmxoff()
{
	return GuestState[KeGetCurrentProcessorNumber()].VmxoffState.GuestRip;
}