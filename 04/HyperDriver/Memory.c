#include <ntddk.h>
#include "Globals.h"
#include "Memory.h"
#include "MSR.h"


BOOLEAN AllocateVmmStack(UINT64 ProcessorID)
{
	vmState[ProcessorID].VMM_STACK = (UINT64)ExAllocatePoolWithTag(NonPagedPool, VMM_STACK_SIZE, POOLTAG);
	if (vmState[ProcessorID].VMM_STACK == (UINT64)NULL)
	{
		KdPrint(("Insufficient memory in allocationg Vmm stack"));
		return FALSE;
	}
	RtlZeroMemory((PUINT64)vmState[ProcessorID].VMM_STACK, VMM_STACK_SIZE);

	return TRUE;
}


BOOLEAN AllocateVMCSRegion(PVCPU vms, BOOLEAN vmxon)
{
	// Legge MSR IA32_VMX_BASIC
	IA32_VMX_BASIC_MSR basic = { 0 };
	basic.Uint64 = __readmsr(MSR_IA32_VMX_BASIC);

	PHYSICAL_ADDRESS PhysicalMax = { 0 };
	PhysicalMax.QuadPart = MAXULONG64;

	// Alloca la memoria per la VMCS (write-back cacheable)
	PUCHAR Buffer = MmAllocateContiguousMemory(basic.Bits.VmcsSize, PhysicalMax);
	if (Buffer == NULL) {
		KdPrint(("[*] Error : Couldn't Allocate Buffer for VMCS Region."));
		return FALSE;
	}

	// Alternativa per indicare anche il tipo di memoria
	/*PHYSICAL_ADDRESS Highest = {0}, Lowest = {0};
	Highest.QuadPart = ~0;

	BYTE* Buffer = MmAllocateContiguousMemorySpecifyCache(basic.Bits.VmcsSize, 
			Lowest, Highest, Lowest, basic.Bits.MemoryType);*/

	UINT64 PhysicalBuffer = MmGetPhysicalAddress(Buffer).QuadPart;

	// Azzera la memoria della VMCS 
	RtlSecureZeroMemory(Buffer, basic.Bits.VmcsSize);

	// Imposta Revision Identifier
	*(UINT64*)Buffer = basic.Bits.VmcsRevisonId;

	// __vmx_on deve essere eseguito presto per permettere l'esecuzione
	// di vmclear, vmptrld, ecc.
	// VMXON prende il puntatore all'indirizzo fisico della VMXON region.
	// PhysicalBuffer è la variabile che contiene tale indirizzo
	// &PhysicalBuffer è il puntatore ad indirizzo da passare.
	int status = 0;
	if (vmxon)
		status = __vmx_on(&PhysicalBuffer);
	if (status)
	{
		KdPrint(("[*] VMXON failed with status %d", status));
		return FALSE;
	}

	// Salva indirizzo di VMXON e VMCS region in variabile globale che conserva 
	// le risorse della CPU virtuale.
	if (!vmxon)
	{
		vms->VMCS_REGION_VA = (UINT64)Buffer;
		vms->VMCS_REGION_PA = PhysicalBuffer;
	}
	else
	{
		vms->VMXON_REGION_VA = (UINT64)Buffer;
		vms->VMXON_REGION_PA = PhysicalBuffer;
	}

	return TRUE;
}