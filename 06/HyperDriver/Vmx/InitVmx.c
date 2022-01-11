#include <ntddk.h>
#include "..\Cpu\Msr.h"
#include "..\Cpu\Cpu.h"
#include "..\Logging\Logging.h"
#include "..\Support\Globals.h"
#include "InitVmx.h"
#include "Vmcs.h"
#include "..\Asm\AsmOperations.h"



BOOLEAN InitializeVmx(UINT64 LogicalProcessors)
{
	// Controlla se è possibile entrare in VMX Operation
	if (!CpuIsVmxSupported())
	{
		LogError("VMX is not supported in this machine !");
		return FALSE;
	}

	// Per ogni processore...
	for (ULONG ProcessorID = 0; ProcessorID < LogicalProcessors; ProcessorID++)
	{
		// ...salva lo stato in un punto all'interno della seguente
		// funzione in modo da recuperarlo al lancio del guest, dopo 
		// essere entrati in VMX operation.
		// Impostando opportunamente la VMCS si farà in modo che il
		// guest cominci ad eseguire il suo codice proprio da tale punto,
		// riprendendo da quando non si era ancora in VMX operation.
		// E' un po' come se il thread fosse stato temporaneamente dirottato 
		// per lanciare il guest e questo cominciasse ad eseguire il codice
		// riprendendo dal punto di deviazione, come se niente in realtà 
		// fosse successo. L'effetto finale è molto interessante in quanto, 
		// in questo modo, si è in grado di rendere guest l'intero sistema 
		// operativo.
		CpuBroadcastRoutine(ProcessorID, (PVOID)AsmSaveState);
	}

	// VMCALL di test per controllare che tutto funzioni.
	if (AsmVmxNonRootVmcall(VMCALL_TEST, 0x1, 0x22, 0x333) == STATUS_SUCCESS)
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}


BOOLEAN VirtualizeCurrentCpu(ULONG ProcessorID, PVOID GuestStack)
{
	Log("============================ Virtualizing Current System (Logical Core : 0x0) ============================", ProcessorID);

	// Imposta i bit di CR0 e CR4 (compreso CR4.VMXE)
	CpuFixBits();

	// Alloca spazio per la VMXON region
	if (!AllocateVMCSRegion(&GuestState[ProcessorID], TRUE))
	{
		LogError("Error in allocating memory for Vmxon region");
		return FALSE;
	}

	// Alloca spazio per la VMCS region
	if (!AllocateVMCSRegion(&GuestState[ProcessorID], FALSE))
	{
		LogError("Error in allocating memory for Vmcs region");
		return FALSE;
	}

	LogInfo("VMCS Region is allocated at  ===============> %llx", GuestState[ProcessorID].VMCS_REGION_VA);
	LogInfo("VMXON Region is allocated at ===============> %llx", GuestState[ProcessorID].VMXON_REGION_VA);

	// Alloca spazio che verrà usato come stack dall'hypervisor durante le Vm exit
	if (!AllocateVmmStack(ProcessorID))
		return FALSE;

	// Alloca spazio la bitmap degli MSR
	if (!AllocateMsrBitmap(ProcessorID))
		return FALSE;

	LogInfo("HOST stack is allocated at =================> %llx", GuestState[ProcessorID].VMM_STACK);
	LogInfo("Msr Bitmap Virtual Address at ==============> %llx", GuestState[ProcessorID].MSR_BITMAP_VA);

	// Inizializza la VMCS collegata al guest da eseguire sul processore corrente.
	LogInfo("Setting up VMCS.");
	SetupVmcs(&GuestState[ProcessorID], GuestStack);

	LogInfo("Executing VMLAUNCH.");

	// Esegue codice guest. 
	__vmx_vmlaunch();

	// Continua in AsmRestoreState...

	//
	// Se VMLAUNCH viene eseguita con successo non si arriverà mai qui...
	//

	// ... In caso contrario legge l'errore ed esegue VMXOFF.
	ULONG64 ErrorCode = 0;
	__vmx_vmread(0x4400, &ErrorCode);
	__vmx_off();
	LogError("VMLAUNCH Error : 0x%llx", ErrorCode);

	return FALSE;
}


BOOLEAN AllocateVmmStack(UINT64 ProcessorID)
{
	GuestState[ProcessorID].VMM_STACK = (UINT64)ExAllocatePoolWithTag(NonPagedPool, VMM_STACK_SIZE, POOLTAG);
	if (GuestState[ProcessorID].VMM_STACK == (UINT64)NULL)
	{
		LogError("Insufficient memory in allocationg Vmm stack");
		return FALSE;
	}
	RtlZeroMemory((PUINT64)GuestState[ProcessorID].VMM_STACK, VMM_STACK_SIZE);

	return TRUE;
}


BOOLEAN AllocateMsrBitmap(INT ProcessorID)
{
	GuestState[ProcessorID].MSR_BITMAP_VA = (UINT64)ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, POOLTAG);

	if (GuestState[ProcessorID].MSR_BITMAP_VA == (UINT64)NULL)
	{
		LogError("Insufficient memory in allocationg Msr bitmaps");
		return FALSE;
	}
	RtlZeroMemory((PUINT64)GuestState[ProcessorID].MSR_BITMAP_VA, PAGE_SIZE);

	GuestState[ProcessorID].MSR_BITMAP_PA = MmGetPhysicalAddress((PUINT64)GuestState[ProcessorID].MSR_BITMAP_VA).QuadPart;

	return TRUE;
}


BOOLEAN AllocateVMCSRegion(PVCPU CurrentGuestState, BOOLEAN VmxOn)
{
	// Legge MSR IA32_VMX_BASIC
	IA32_VMX_BASIC_MSR basic = { 0 };
	basic.Uint64 = __readmsr(MSR_IA32_VMX_BASIC);

	PHYSICAL_ADDRESS PhysicalMax = { 0 };
	PhysicalMax.QuadPart = MAXULONG64;

	// Alloca la memoria per la VMCS (write-back cacheable)
	PUCHAR Buffer = MmAllocateContiguousMemory(basic.Bits.VmcsSize, PhysicalMax);
	if (Buffer == NULL) {
		LogError("Error : Couldn't Allocate Buffer for VMCS Region.");
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
	if (VmxOn)
		status = __vmx_on(&PhysicalBuffer);
	if (status)
	{
		LogError("VMXON failed with status %d", status);
		return FALSE;
	}

	// Salva indirizzo di VMXON e VMCS region in variabile globale che conserva 
	// le risorse della CPU virtuale.
	if (!VmxOn)
	{
		CurrentGuestState->VMCS_REGION_VA = (UINT64)Buffer;
		CurrentGuestState->VMCS_REGION_PA = PhysicalBuffer;
	}
	else
	{
		CurrentGuestState->VMXON_REGION_VA = (UINT64)Buffer;
		CurrentGuestState->VMXON_REGION_PA = PhysicalBuffer;
	}

	return TRUE;
}