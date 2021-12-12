#include <ntddk.h>
#include "Cpu.h"
#include "Msr.h"

BOOLEAN CpuIsVmxSupported()
{
	CPUID_EAX_01 data = { 0 };

	// Controlla il bit VMX (il quinti bit in ecx).
	// L'intrinsics __cpuid prende un array di interi come primo param.
	// (che indica dove mettere le info di output ritornate nei registri 
	// EAX, EBX, ECX ed EDX) ed il valore di input da mettere in EAX nel 
	// secondo param. (che indica il tipo di info da recuperare).
	// La struct CPUID_EAX_1 è definita con 4 campi interi contigui 
	// quindi può essere trattata come fosse un array.
	__cpuid((PINT32)&data, 1);

	if (!_bittest((PLONG)&data.ecx, 5))   //equiv. a: if ((data.ecx & (1 << 5)) == 0)
		return FALSE;

	// Usa l'intrinsics __readmsr per leggere l'MSR IA32_FEATURE_CONTROL
	IA32_FEATURE_CONTROL_MSR Control = { 0 };
	Control.Uint64 = __readmsr(MSR_IA32_FEATURE_CONTROL);

	// Controlla il bit 2 di IA32_FEATURE_CONTROL.
	// Se non è impostato ad 1 vuol dire che la virtualizzazione
	// è disabilitata dal BIOS e bisogna attivarla.
	if (Control.Bits.EnableVmxOutsideSmx == FALSE)
	{
		KdPrint(("[*] Please enable Virtualization from BIOS"));
		return FALSE;
	}

	return TRUE;
}

VOID CpuVmxEnable(BOOLEAN enable)
{
	CR4 Register;

	// Recupera registro CR4
	Register.Uint64 = __readcr4();

	// Imposta il bit 13 (VMX Enable) in base al parametro di input
	if (enable)
		Register.Bits.VMXE = 1;
	else
		Register.Bits.VMXE = 0;

	// Scrive il nuovo valore nel registro CR4
	__writecr4(Register.Uint64);
}

VOID CpuFixBits()
{
	CR4 Cr4 = { 0 };
	CR0 Cr0 = { 0 };

	// Fix Cr0
	Cr0.Uint64 = __readcr0();
	Cr0.Uint64 |= __readmsr(MSR_IA32_VMX_CR0_FIXED0);
	Cr0.Uint64 &= __readmsr(MSR_IA32_VMX_CR0_FIXED1);
	__writecr0(Cr0.Uint64);

	// Fix Cr4
	Cr4.Uint64 = __readcr4();
	Cr4.Uint64 |= __readmsr(MSR_IA32_VMX_CR4_FIXED0);
	Cr4.Uint64 &= __readmsr(MSR_IA32_VMX_CR4_FIXED1);
	__writecr4(Cr4.Uint64);
}

BOOLEAN CpuBroadcastRoutine(ULONG ProcessorIndex, PVOID Routine)
{
	KIRQL OldIrql;
	PROCESSOR_NUMBER ProcessorNumber;
	GROUP_AFFINITY Affinity, OldAffinity;

	// Converte da indice di sistema ad indice locale nel relativo gruppo
	KeGetProcessorNumberFromIndex(ProcessorIndex, &ProcessorNumber);

	// Imposta l'affinità del thread corrente per eseguirlo sull'i-esimo processore
	RtlSecureZeroMemory(&Affinity, sizeof(GROUP_AFFINITY));
	Affinity.Group = ProcessorNumber.Group;
	Affinity.Mask = (KAFFINITY)((ULONG64)1 << ProcessorNumber.Number);
	KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

	// Eleva IRQL a DISPATCH_LEVEL per impedire context switch
	OldIrql = KeRaiseIrqlToDpcLevel();

	// Esegue funzione passata come secondo param. di CpuBroadcastRoutine e 
	// le passa il numero del processore corrente come unico argomento.
	((void(*)(ULONG))Routine)(ProcessorNumber.Number);

	// Continua in AsmSaveState...
	// ...Ritorna dopo AsmRestoreState

	//
	// Tutto il codice eseguito da questo momento in poi da questo
	// processore (anche quello che verrà eseguito una volta usciti
	// dal modulo del driver corrente) è codice guest in VMX non-root 
	// operation. In altre parole, l'intero sistema operativo Windows 
	// è il guest.
	//

	// Ristabilisce IRQL e maschera.
	KeLowerIrql(OldIrql);
	KeRevertToUserGroupAffinityThread(&OldAffinity);

	return TRUE;
}