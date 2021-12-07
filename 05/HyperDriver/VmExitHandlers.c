#include <ntddk.h>
#include "VmxRoot.h"
#include "Globals.h"
#include "Msr.h"
#include "VmExitHandlers.h"


VOID VmxRootExitHandleMsrRead(PGUEST_REGS GuestRegs)
{
	MSR msr = { 0 };


	// L'istruzione RDMSR causa VM exit se almeno una delle seguenti condizioni è vera:
	// 
	// Il bit "use MSR bitmaps" del primary processor-based è 0.
	// Il valore di ECX è fuori dai range [0x00000000, 0x00001FFF] e [0xC0000000, 0xC0001FFF]
	// Il valore di ECX è nel range [0x00000000, 0x00001FFF] ed il bit n nella bitmap degli
	// MSR bassi è 1, dove n è il valore di ECX.
	// Il valore di ECX è nel range [0xC0000000, 0xC0001FFF] ed il bit n nella bitmap degli
	// MSR alti è 1, dove n è il valore di ECX & 0x00001FFF.

	// Controlla che ECX sia nei range validi per leggere un MSR oppure sia nel range
	// riservato [0x40000000, 0x400000F0], usato da Windows per chiedere o riportare
	// informazioni ad Hyper-V.
	if ((GuestRegs->rcx <= 0x00001FFF) || ((0xC0000000 <= GuestRegs->rcx) && (GuestRegs->rcx <= 0xC0001FFF))
		|| (GuestRegs->rcx >= RESERVED_MSR_RANGE_LOW && (GuestRegs->rcx <= RESERVED_MSR_RANGE_HI)))
	{
		// Esegue RDMSR in VMX root operation, azione che ripassa la palla a Hyper-V
		// per la gestione di tale istruzione. Alla fine si ritorna qui in quanto dal
		// punto di vista di Hyper-V il nostro hypervisor è solo un guest speciale:
		// la nostra è una root operation fittizia ed eseguire RDMSR in tale modalità
		// causa una VM exit che fa capire ad Hyper-V che non siamo interessati a
		// gestirla e che se ne dovrebbe occupare lui. Dopo che Hyper-V ha gestito
		// la VM exit esegue VMRESUME e si riprende da questo punto come se il nostro
		// hypervisor fosse un normalissimo guest.
		msr.Uint64 = __readmsr(GuestRegs->rcx);
	}

	// Salva il risultato della lettura dell'MSR nei registri del guest.
	// RDMSR prende da ECX l'indirizzo dell'MSR da leggere e mette il 
	// risultato in EAX:EDX.
	GuestRegs->rax = msr.Low;
	GuestRegs->rdx = msr.High;
}


VOID VmxRootExitHandleMsrWrite(PGUEST_REGS GuestRegs)
{
	MSR msr = { 0 };

	// L'istruzione WRMSR causa VM exit alle stesse condizioni viste in VmExitHandleMsrRead
	// per RDMSR.

	// Controlla che ECX sia nei range validi per leggere un MSR oppure sia nel range
	// riservato [0x40000000, 0x400000F0], usato da Windows per chiedere o riportare
	// informazioni ad Hyper-V.
	if ((GuestRegs->rcx <= 0x00001FFF) || ((0xC0000000 <= GuestRegs->rcx) && (GuestRegs->rcx <= 0xC0001FFF))
		|| (GuestRegs->rcx >= RESERVED_MSR_RANGE_LOW && (GuestRegs->rcx <= RESERVED_MSR_RANGE_HI)))
	{
		// WRMSR prende da ECX l'indirizzo dell'MSR e legge il valore da scrivere
		// in tale registro da EAX:EDX.
		// Si noti che RAX ed RDX sono quelli del guest.
		msr.Low = (ULONG)GuestRegs->rax;
		msr.High = (ULONG)GuestRegs->rdx;

		// Esegue WRMSR in VMX root operation, azione che ripassa la palla a Hyper-V
		// per la gestione di tale istruzione. Alla fine si ritorna qui in quanto dal
		// punto di vista di Hyper-V il nostro hypervisor è solo un guest speciale:
		// la nostra è una root operation fittizia ed eseguire WRMSR in tale modalità
		// causa una VM exit che fa capire ad Hyper-V che non siamo interessati a
		// gestirla e che se ne dovrebbe occupare lui. Dopo che Hyper-V ha gestito
		// la VM exit esegue VMRESUME e si riprende da questo punto come se il nostro
		// hypervisor fosse un normalissimo guest.
		__writemsr(GuestRegs->rcx, msr.Uint64);
	}
}


VOID VmxRootExitHandleCpuid(PGUEST_REGS RegistersState)
{
	INT32 cpu_info[4];


	// Ripassa la palla ad Hyper-V eseguendo CPUIDEX nella root operation fittizia,
	// che causa una nuova VM exit che indica ad Hyper-V che non eravamo interessati
	// alla gestione della VM exit originaria.
	__cpuidex(cpu_info, (INT32)RegistersState->rax, (INT32)RegistersState->rcx);


	if (RegistersState->rax == 0x1)
	{
		// Se il valore in RAX del guest è 1 significa che il guest è interessato
		// a conoscere le funzionalità supportata dal processore.
		// Il 32_esimo bit (bit alla posizione 31) di ECX normalmente ritorna sempre 0 
		// ma Intel lo riserva come bit per segnalare la presenza di un hypervisor
		// a del codice guest che è interessato a sapere tale informazione.
		// Dato che siamo in effetti su un sistema virtualizzato e che il nostro
		// hypervisor è quello a cui Hyper-V passa tutte le VM exit è bene
		// impostare tale bit per confermare al guest la presenza dell'hypervisor.
		cpu_info[2] |= 0x80000000;
	}
	else if (RegistersState->rax == 0x40000000)
	{
		// I valori nell'intervallo [0x40000000, 0x4FFFFFFF] non vengono normalmente
		// accettati come input validi da mettere in EAX e da passare a CPUID.
		// Intel però riserva i valori in [0x40000000, 0x400000FF] per fornire 
		// ulteriori informazioni al guest sull'hypervisor che lo controlla.
		// Il valore 0x40000000 indica che il guest è interessato a sapere
		// chi è l'hypervisor (Vendor ID signature) e qual'è il valore massimo 
		// che accetta per EAX (nell'intervallo [0x40000001, 0x400000FF])
		// quando deve essere usato come operatore nell'esecuzione di CPUID.
		cpu_info[0] = 0x40000001;
		cpu_info[1] = 'sroC';  // "Corso VT-x"
		cpu_info[2] = 'TV o';
		cpu_info[3] = '\x00\x00x-';
	}
	else if (RegistersState->rax == 0x40000001)
	{
		// Se il valore in RAX del guest è 0x40000001 significa che il guest 
		// è interessato a sapere se l'hypervisor fornisce almeno il supporto 
		// minimo richiesto da un hypervisor conforme al Microsoft Hypervisor 
		// Interface.
		// In questo caso segnaliamo che il nostro hypervisor non è conforme
		// (restituendo Hv#0 invece che Hv#1) perché siamo solo interessati
		// alle richieste del guest quando queste vengono esclusivamente dal
		// nostro driver eseguito in non-root operation e non da altre parti
		// (in quel caso si ripassa semplicemente la palla ad Hyper-V).
		cpu_info[0] = '0#vH';  // Hv#0
		cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
	}

	// Copia i valori nei registri del guest.
	RegistersState->rax = cpu_info[0];
	RegistersState->rbx = cpu_info[1];
	RegistersState->rcx = cpu_info[2];
	RegistersState->rdx = cpu_info[3];

}


NTSTATUS VmxRootVmcallHandler(UINT64 VmcallNumber, UINT64 OptionalParam1, UINT64 OptionalParam2, UINT64 OptionalParam3)
{
	NTSTATUS VmcallStatus;

	VmcallStatus = STATUS_UNSUCCESSFUL;

	// I primi 32 bit sono sufficienti a distinguere tra (2^(32) - 1) VMCALL diverse.
	// Questo permette di usare, volendo, i restanti 32 bit alti per altro.
	switch (VmcallNumber & 0xffffffff)
	{

	case VMCALL_TEST:
	{
		VmcallStatus = VmxRootVmCallTest(OptionalParam1, OptionalParam2, OptionalParam3);
		break;
	}

	case VMCALL_VMXOFF:
	{
		VmxRootVmxoff();
		VmcallStatus = STATUS_SUCCESS;
		break;
	}

	default:
	{
		VmcallStatus = STATUS_UNSUCCESSFUL;
		break;
	}

	}

	return VmcallStatus;
}