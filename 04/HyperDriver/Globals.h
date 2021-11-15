#pragma once

// Variabili globali in cui salvare il valore dei registri RSP ed RBP
// al fine di ritornare nel codice di DriverEntry una volta che si
// decide di uscire dalla VMX operation.
UINT64 g_StackPointerForReturning;
UINT64 g_BasePointerForReturning;

// Risorse allocate nella virtualizzazione di ogni processore logico.
typedef struct _VCPU
{
	UINT64 VMXON_REGION_VA;			// Indirizzo virtuale di regione VMXON
	UINT64 VMXON_REGION_PA;			// Indirizzo fisico di regione VMXON
	UINT64 VMCS_REGION_VA;			// Indirizzo virtuale di regione VMCS
	UINT64 VMCS_REGION_PA;			// Indirizzo fisico di regione VMCS
	UINT64 VMM_STACK;				// Stack dell'hypervisor durante VM-Exit
	UINT64 GuestRipVA;              // VM Entry point
	GROUP_AFFINITY OldAffinity;     // Original Thread Affinity
}VCPU, * PVCPU;

// Variabile globale che punterà ad un array di VCPU, con le risorse
// allocate per tutti i processori.
extern PVCPU vmState;