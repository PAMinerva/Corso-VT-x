#include <ntddk.h>
#include "VMX.h"
#include "MSR.h"
#include "CPU.h"
#include "DPC.h"
#include "Globals.h"
#include "Memory.h"
#include "VMCS.h"



// Imposta a clear lo stato della VMCS e svuota la cache usando Vmclear.
BOOLEAN VmxClearVmcs(PVCPU CurrentVmState)
{
	// La VMCS passata come parametro diventa inattiva.
	int VmclearStatus = __vmx_vmclear(&CurrentVmState->VMCS_REGION_PA);

	KdPrint(("[*] Vmcs Vmclear Status : %d\n", VmclearStatus));

	if (VmclearStatus)
	{
		// Se qualcosa è andato storto esegue VMOFF.
		KdPrint(("[*] VMCS failed to clear ( status : %d )", VmclearStatus));
		__vmx_off();
		return FALSE;
	}
	return TRUE;
}

// Imposta la VMCS come corrente ed attiva usando l'istruzione Vmptrld.
BOOLEAN VmxLoadVmcs(PVCPU CurrentGuestState) {

	int VmptrldStatus = __vmx_vmptrld(&CurrentGuestState->VMCS_REGION_PA);

	KdPrint(("[*] Vmcs Vmptrld Status : %d\n", VmptrldStatus));

	if (VmptrldStatus)
	{
		KdPrint(("[*] VMCS failed to load ( status : %d )", VmptrldStatus));
		return FALSE;
	}
	return TRUE;
}


// Inizializza VMX, VMCS e lancia il guest
BOOLEAN VmxInitialize(UINT64 LogicalProcessors, ULONG ProcessorIndex)
{
	// Controlla se è possibile entrare in VMX Operation
	if (!CpuIsVMXSupported())
	{
		KdPrint(("[*] VMX is not supported in this machine !"));
		return FALSE;
	}

	KIRQL OldIrql;
	PROCESSOR_NUMBER ProcessorNumber;
	GROUP_AFFINITY Affinity, OldAffinity;

	KdPrint(("\n=====================================================\n"));

	for (ULONG i = 0; i < LogicalProcessors; i++)
	{
		// Converte da indice di sistema ad indice locale nel relativo gruppo
		KeGetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);

		// Imposta affinità del thread corrente per eseguirlo su i-esimo processore
		RtlSecureZeroMemory(&Affinity, sizeof(GROUP_AFFINITY));
		Affinity.Group = ProcessorNumber.Group;
		Affinity.Mask = (KAFFINITY)((ULONG64)1 << ProcessorNumber.Number);
		KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

		KdPrint(("\n\t\tCurrent thread is executing in %d th logical processor.\n\n", ProcessorIndex));

		// Eleva IRQL a DISPATCH_LEVEL per impedire context switch
		OldIrql = KeRaiseIrqlToDpcLevel();

		// Imposta i bit di CR0 e CR4 (compreso CR4.VMXE)
		CpuFixBits();

		KdPrint(("[*] VMX-Operation Enabled Successfully\n"));

		// Ristabilisce maschera di affinità del thread..
		KdPrint(("[*] Saving old thread affinity.\n"));
		vmState[i].OldAffinity = OldAffinity;

		if (!AllocateVMCSRegion(&vmState[i], TRUE))
		{
			KdPrint(("[*] Error in allocating memory for Vmxon region"));
			return FALSE;
		}
		if (!AllocateVMCSRegion(&vmState[i], FALSE))
		{
			KdPrint(("[*] Error in allocating memory for Vmcs region"));
			return FALSE;
		}

		KdPrint(("[*] VMCS Region is allocated at  ===============> %llx\n", vmState[i].VMCS_REGION_VA));
		KdPrint(("[*] VMXON Region is allocated at ===============> %llx\n", vmState[i].VMXON_REGION_VA));

		//Allocating VMM Stack
		if (!AllocateVmmStack(i))
		{
			// Some error in allocating Vmm Stack
			return FALSE;
		}

		KdPrint(("[*] HOST stack is allocated at =================> %llx\n", vmState[i].VMM_STACK));

		// Alloca memoria che conterrà codice del guest.
		// In questo caso si tratta di una semplice pagina che inizia con l'istruzione HLT.
		vmState[i].GuestRipVA = (UINT64)ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, POOLTAG);
		if (vmState[i].GuestRipVA == (UINT64)NULL)
			return FALSE;
		RtlZeroMemory((PUINT64)vmState[i].GuestRipVA, PAGE_SIZE);
		void* TempAsm = "\xF4"; // F4 opcode di HLT
		memcpy((PUINT64)vmState[i].GuestRipVA, TempAsm, 1);

		KdPrint(("[*] HOST RIP is allocated at ===================> %llx\n", vmState[i].GuestRipVA));

		// Imposta a clear lo stato della VMCS.
		if (!VmxClearVmcs(&vmState[i])) {
			return FALSE;
		}

		// Imposta la VMCS come corrente ed attiva.
		if (!VmxLoadVmcs(&vmState[i]))
		{
			return FALSE;
		}

		KdPrint(("[*] Setting up VMCS.\n"));
		SetupVmcs(&vmState[i], vmState[i].GuestRipVA);

		// Ristabilisce IRQL.
		KeLowerIrql(OldIrql);

		DbgPrint("[*] Executing VMLAUNCH.\n");
		KdPrint(("\n=====================================================\n"));

		// Dopo VMLAUNCH il controllo viene passato al guest.
		// In questo caso il guest eseguirà HLT che causerà una VM exit, che verrà gestita
		// semplicemente spegnendo tutto con VMXOFF. Cosa succede dopo?
		// L'idea è quella di proseguire da qui, ed in particolare dal return TRUE
		// finale che ritorna al codice chiamante VmxInitialize (DriverEntry in questo caso)
		// per proseguire normalmente. 
		// Il problema è che return TRUE non verrà mai eseguita. Quindi? 
		// Se si memorizzano i registri RSP ed RBP in questo punto è possibile ripristinarli 
		// successivamente nel gestore della VM exit provocata dall'HLT. A quel punto
		// si può manipolare lo stack e ricalcolare manualmente l'indirizzo di ritorno all'
		// interno del chiamante di VmxInitialize. Per simulare il valore di ritorno, invece,
		// è sufficiente impostare RAX ad 1 (si veda codice di AsmSaveVMXOFFState).
		AsmSaveVMXOFFState();


		// Esegue codice guest.
		// In questo caso esegue HLT, che provoca una VM exit.
		// Quindi si può proseguire andando a vedere il codice di AsmVMExitHandler. 
		__vmx_vmlaunch();

		//
		// Se VMLAUNCH viene eseguita con successo non si arriverà mai qui !!!
		//

		// In caso contrario legge l'errore ed esegue VMXOFF.
		ULONG64 ErrorCode = 0;
		__vmx_vmread(VM_INSTRUCTION_ERROR, &ErrorCode);
		__vmx_off();
		KdPrint(("[*] VMLAUNCH Error : 0x%llx\n", ErrorCode));
		DbgBreakPoint();
	}

	// Se tutto è andato per il meglio non si arriverà mai qui.
	return TRUE;
}


VOID VmxVMExitHandler()
{
	ULONG ExitReason = 0;
	__vmx_vmread(VM_EXIT_REASON, &ExitReason);


	ULONG ExitQualification = 0;
	__vmx_vmread(VM_EXIT_QUALIFICATION, &ExitQualification);

	KdPrint(("\nEXIT_REASION 0x%x\n", ExitReason & 0xffff));
	KdPrint(("EXIT_QUALIFICATION 0x%x\n\n", ExitQualification));


	switch (ExitReason)
	{
		case EXIT_REASON_HLT:
		{
			DbgPrint("[*] Execution of HLT detected... \n");

			// DbgBreakPoint();

			// Spegne tutto con VMXOFF e dirotta in DriverEntry.
			AsmRestoreToVMXOFFState();

			break;
		}
		default:
		{
			// DbgBreakPoint();
			break;

		}
	}
}

VOID VmxTerminate(UINT64 LogicalProcessors, ULONG ProcessorIndex)
{
	PROCESSOR_NUMBER ProcessorNumber;
	GROUP_AFFINITY Affinity, OldAffinity;

	for (ULONG i = 0; i < LogicalProcessors; i++)
	{
		KeGetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);

		RtlSecureZeroMemory(&Affinity, sizeof(GROUP_AFFINITY));
		Affinity.Group = ProcessorNumber.Group;
		Affinity.Mask = (KAFFINITY)((ULONG64)1 << ProcessorNumber.Number);
		KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

		if (vmState && vmState[i].VMXON_REGION_VA)
			MmFreeContiguousMemory((PUINT64)vmState[i].VMXON_REGION_VA);

		if (vmState && vmState[i].VMCS_REGION_VA)
			MmFreeContiguousMemory((PUINT64)vmState[i].VMCS_REGION_VA);

		if (vmState && vmState[i].VMM_STACK)
			ExFreePoolWithTag((PUINT64)vmState[i].VMM_STACK, POOLTAG);

		if (vmState && vmState[i].GuestRipVA)
			ExFreePoolWithTag((PUINT64)vmState[i].GuestRipVA, POOLTAG);

		if (vmState)
			ExFreePoolWithTag((PUINT64)vmState, POOLTAG);

		KeRevertToUserGroupAffinityThread(&OldAffinity);
	}
}