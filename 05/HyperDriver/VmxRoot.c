#include <ntddk.h>
#include "VmxRoot.h"
#include "Cpu.h"
#include "Msr.h"
#include "Globals.h"
#include "Vmcs.h"
#include "VmExitHandlers.h"
#include "Asm\VmIntrinsics.h"
#include "Asm\AsmOperations.h"



VOID VmxRootResumeToNextInstruction()
{
	ULONG64 ResumeRIP = 0;
	ULONG64 CurrentRIP = 0;
	ULONG ExitInstructionLength = 0;

	__vmx_vmread(GUEST_RIP, &CurrentRIP);
	__vmx_vmread(VM_EXIT_INSTRUCTION_LENGTH, &ExitInstructionLength);

	ResumeRIP = CurrentRIP + ExitInstructionLength;

	__vmx_vmwrite(GUEST_RIP, ResumeRIP);
}


VOID VmxRootRestoreRegisters()
{
	ULONG64 FsBase;
	ULONG64 GsBase;
	ULONG64 GdtrBase;
	ULONG GdtrLimit;
	ULONG64 IdtrBase;
	ULONG IdtrLimit;

	// Restore FS Base 
	__vmx_vmread(GUEST_FS_BASE, &FsBase);
	__writemsr(MSR_IA32_FS_BASE, FsBase);

	// Restore Gs Base
	__vmx_vmread(GUEST_GS_BASE, &GsBase);
	__writemsr(MSR_IA32_GS_BASE, GsBase);

	// Restore GDTR
	__vmx_vmread(GUEST_GDTR_BASE, &GdtrBase);
	__vmx_vmread(GUEST_GDTR_LIMIT, &GdtrLimit);

	__reload_gdtr(GdtrBase, GdtrLimit);

	// Restore IDTR
	__vmx_vmread(GUEST_IDTR_BASE, &IdtrBase);
	__vmx_vmread(GUEST_IDTR_LIMIT, &IdtrLimit);

	__reload_idtr(IdtrBase, IdtrLimit);
}


BOOLEAN VmxRootVmExitHandler(PGUEST_REGS GuestRegs)
{
	ULONG ExitReason = 0;
	__vmx_vmread(VM_EXIT_REASON, &ExitReason);

	ULONG ExitQualification = 0;
	__vmx_vmread(VM_EXIT_QUALIFICATION, &ExitQualification);

	ULONG CurrentProcessorIndex = KeGetCurrentProcessorNumber();

	// Segnala che si è in VMX root operation e che, normalmente,
	// si dovrebbe procedere a rieseguire il guest a partire
	// dall'istruzione succesiva a quella che ha causato la
	// VM exit (altrimenti si entra in un loop infinito di
	// VM exit e VM entry).
	GuestState[CurrentProcessorIndex].IsOnVmxRootMode = TRUE;
	GuestState[CurrentProcessorIndex].IncrementRip = TRUE;


	switch (ExitReason)
	{
		// VM exit causata dalla generazione di una eccezione durante la gestione di un
		// doppio fault, che a sua volta tenta di gestire una eccezione generata durante
		// la gestione di una eccezione.
		// In questo caso non c'è molto da fare se non breakkare e capire cosa non va.
		case EXIT_REASON_TRIPLE_FAULT:
		{
			KdPrint(("Triple fault error occured."));

			break;
		}

		// Istruzioni che causano VM Exit in maniera incondizionata (cioè semplicemente
		// eseguendole, da parte del guest in non-root operation, a prescindere dalle 
		// impostazioni nella VMCS): CPUID, GETSEC, INVD, e XSETBV. 
		// Questo vale anche per tutte le istruzioni VMX:
		// INVEPT, INVVPID, VMCALL, VMCLEAR, VMLAUNCH, VMPTRLD, VMPTRST, VMRESUME, VMXOFF e VMXON.
		// Dalla documentazione:
		/*
			VMX instructions are allowed only in VMX root operation.
			An attempt to execute a VMX instruction in VMX non-root operation causes a VM exit.
			Processors perform various checks while executing any VMX instruction.
			They follow well-defined error handling on failures. VMX instruction execution
			failures detected before loading of a guest state are handled by the processor as follows:

			- If the working-VMCS pointer is not valid, the instruction fails by setting RFLAGS.CF to 1

			Software is required to check RFLAGS.CF to determine the success or failure of
			VMX instruction executions.
		*/
		// Dato che, a parte VMCALL, non c'è motivo di permettere l'esecuzione di istruzioni VMX nel 
		// codice guest si imposterà semplicemente RFLAGS.CF ad 1 per indicare il fallimento di tutte 
		// le istruzione VMX in non-root operation, esclusa VMCALL.

		case EXIT_REASON_VMCLEAR:
		case EXIT_REASON_VMPTRLD:
		case EXIT_REASON_VMPTRST:
		case EXIT_REASON_VMREAD:
		case EXIT_REASON_VMRESUME:
		case EXIT_REASON_VMWRITE:
		case EXIT_REASON_VMXOFF:
		case EXIT_REASON_VMXON:
		case EXIT_REASON_VMLAUNCH:
		{

			ULONG64 Rflags = 0;
			__vmx_vmread(GUEST_RFLAGS, &Rflags);
			__vmx_vmwrite(GUEST_RFLAGS, Rflags | 0x1); // RFLAGS.CF è il primo bit

			break;
		}

		// Gestisce VM exit causate da RDMSR
		case EXIT_REASON_MSR_READ:
		{
			VmxRootExitHandleMsrRead(GuestRegs);

			break;
		}

		// Gestisce VM exit causate da WRMSR
		case EXIT_REASON_MSR_WRITE:
		{
			VmxRootExitHandleMsrWrite(GuestRegs);

			break;
		}

		// Gestisce VM exit causate da CPUID
		case EXIT_REASON_CPUID:
		{
			VmxRootExitHandleCpuid(GuestRegs);

			break;
		}

		// Gestisce VM exit causate da VMCALL
		case EXIT_REASON_VMCALL:
		{
			// Controlla se è stata una delle nostre funzioni (quelle contenute nel
			// driver che implementa l'hypervisor e che vengono eseguite in non-root 
			// operation in quanto ora l'intero sistema è il guest) ad eseguire la 
			// VMCALL oppure se è stato qualcun'altro che intendeva rivolgersi ad Hyper-V.
			// Nota che, a prescindere dalla funzione invocata, il valore di ritorno
			// viene caricato nel registro RAX del guest, in modo che questo lo possa 
			// vedere una volta tornati in non-root operation.
			if (GuestRegs->r10 == 0x4e4f485950455256 && GuestRegs->r11 == 0x564d43414c4c)
			{
				// La VMCALL è roba nostra e la gestiamo nel nostro hypervisor
				// Salva il valore di ritorno di VmxRootVmcallHandler in RAX del guest:
				// in questo modo è possibile informare il codice guest sulla
				// corretta gestione o meno della VMCALL da parte dell'hypervisor.
				GuestRegs->rax = VmxRootVmcallHandler(GuestRegs->rcx, GuestRegs->rdx, GuestRegs->r8, GuestRegs->r9);
			}
			else
			{
				// Rimandiamo la VMCALL a Hyper-V perché è roba sua.
				// Il valore di ritorno della HyperCall viene salvato in RAX del guest.
				GuestRegs->rax = AsmHypervVmcall(GuestRegs->rcx, GuestRegs->rdx, GuestRegs->r8);
			}
			break;
		}

		// Non si vorrebbe mai fermare il processore quindi breakka per capire cosa non va.
		case EXIT_REASON_HLT:
		{
			//__halt();
			break;
		}

		default:
		{
			KdPrint(("Unkown Vmexit, reason : 0x%llx", ExitReason));
			break;
		}

	}

	// Se siamo ancora in VMX operation e non è necessario ripetere l'istruzione che ha portato
	// a causare la VM exit corrente allora si deve avanzare alla prossima istruzione del guest
	// prima di eseguire VMRESUME.
	if (!GuestState[CurrentProcessorIndex].VmxoffState.IsVmxoffExecuted && GuestState[CurrentProcessorIndex].IncrementRip)
	{
		VmxRootResumeToNextInstruction();
	}

	// Segnala che si sta uscendo dalla VMX root operation
	GuestState[CurrentProcessorIndex].IsOnVmxRootMode = FALSE;

	// Se si è eseguito VMXOFF ritorna TRUE a AsmVmExitHandler...
	if (GuestState[CurrentProcessorIndex].VmxoffState.IsVmxoffExecuted)
	{
		return TRUE;
	}

	// ... altrimenti ritorna FALSE. 
	return FALSE;
}


// Ritorna ad eseguire il codice guest tramite VMRESUME
VOID VmxRootVmresume()
{
	ULONG64 ErrorCode;

	__vmx_vmresume();

	// Se VMRESUME viene eseguita correttamente non si arriverà mai qui.

	ErrorCode = 0;
	__vmx_vmread(VM_INSTRUCTION_ERROR, &ErrorCode);
	__vmx_off();

	KdPrint(("Error in executing Vmresume , status : 0x%llx", ErrorCode));
	DbgBreakPoint();
}


NTSTATUS VmxRootVmCallTest(UINT64 Param1, UINT64 Param2, UINT64 Param3)
{
	KdPrint(("VmcallTest called with @Param1 = 0x%llx , @Param2 = 0x%llx , @Param3 = 0x%llx\n", Param1, Param2, Param3));
	return STATUS_SUCCESS;
}


VOID VmxRootVmxoff()
{
	int CurrentProcessorIndex;
	UINT64 GuestRSP = 0;
	UINT64 GuestRIP = 0;
	UINT64 ExitInstructionLength = 0;

	CurrentProcessorIndex = KeGetCurrentProcessorNumber();

	// Legge guest RSP e RIP al momento della VMCALL che ha causato la VM exit
	// che porterà ad eseguire VMXOFF.
	// Questi valori verranno usati in AsmVmxoffHandler per ritornare all'
	// istruzione successiva alla VMCALL in AsmVmxNonRootVmcall.
	__vmx_vmread(GUEST_RIP, &GuestRIP);
	__vmx_vmread(GUEST_RSP, &GuestRSP);

	// Legge la lunghezza in byte di VMCALL e la aggiunge a RIP per
	// farlo puntare all'istruzione successiva: pop r11 in AsmVmxNonRootVmcall.
	__vmx_vmread(VM_EXIT_INSTRUCTION_LENGTH, &ExitInstructionLength);
	GuestRIP += ExitInstructionLength;

	// Salva il risultato nello stato del processore prima di eseguire VMXOFF.
	// Tali dati serviranno a riprendere l'esecuzione da dove si era interrotta
	// una volta usciti dalla VMX operation (poiché non c'è più possibilità di
	// usare VMRESUME per riprendere il codice guest).
	GuestState[CurrentProcessorIndex].VmxoffState.GuestRip = GuestRIP;
	GuestState[CurrentProcessorIndex].VmxoffState.GuestRsp = GuestRSP;

	// Ripristina FS, GS , GDTR e IDTR poiché, se volutamente o per errore
	// qualcosa è cambiato, patchguard potrebbe non prenderla benissimo.
	VmxRootRestoreRegisters();

	// Esegue VMXOFF per uscire dalla VMX operation.
	__vmx_off();

	// Notifica che si è eseguita VMXOFF e non si è più in VMX operation.
	GuestState[CurrentProcessorIndex].VmxoffState.IsVmxoffExecuted = TRUE;

	// Dato che si è usciti dalla VMX operation si può disabilitare CR4.VMXE
	CpuVmxEnable(FALSE);
}