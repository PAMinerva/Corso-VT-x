#include <ntddk.h>
#include "VMX.h"
#include "CPU.h"
#include "DPC.h"

VOID VmxInitializeDpc(
	PKDPC Dpc, 
	PVOID DeferredContext, 
	PVOID SystemArgument1, 
	PVOID SystemArgument2)
{
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(DeferredContext);

	CpuFixBits();

	// In pratica SystemArgument1 e SystemArgument2 sono usati solo come
	// oggetti di sincronizzazione (hanno valori di default).

	// Aspetta che tutte le DPC in esecuzione sui vari processori logici
	// arrivino a questo punto.
	KeSignalCallDpcSynchronize(SystemArgument2);

	// Segnala la DPC come completata.
	KeSignalCallDpcDone(SystemArgument1);
}

VOID VmxInitializeAffinity()
{
	ULONG LogicalProcessorNumber = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	KIRQL OldIrql;
	PROCESSOR_NUMBER ProcessorNumber;
	GROUP_AFFINITY Affinity, OldAffinity;


	for (ULONG i = 0; i < LogicalProcessorNumber; i++)
	{
		// Converte da indice di sistema ad indice locale nel relativo gruppo
		KeGetProcessorNumberFromIndex(i, &ProcessorNumber);

		// Imposta affinità del thread corrente per eseguirlo su i-esimo processore
		RtlSecureZeroMemory(&Affinity, sizeof(GROUP_AFFINITY));
		Affinity.Group = ProcessorNumber.Group;
		Affinity.Mask = (KAFFINITY)(1 << ProcessorNumber.Number);
		KeSetSystemGroupAffinityThread(&Affinity, &OldAffinity);

		// Eleva IRQL a DISPATCH_LEVEL per impedire context switch
		OldIrql = KeRaiseIrqlToDpcLevel();

		// Esegui su ogni processore logico.
		CpuFixBits();

		// Ristabilisce IRQL e maschera.
		KeLowerIrql(OldIrql);
		KeRevertToUserGroupAffinityThread(&OldAffinity);
	}
}