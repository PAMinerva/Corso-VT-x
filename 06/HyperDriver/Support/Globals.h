#pragma once


#define POOLTAG             0x56542d78 // VT-x
#define VMM_STACK_SIZE      0x5000     // Kernel stack è 16 o 20 KB


#define VMCALL_TEST						0x1			// Testa VMCALL
#define VMCALL_VMXOFF					0x2			// Usa VMCALL per eseguire VMXOFF


// Se si esegue VMXOFF in root operation poi non si può più usare VMRESUME
// per riprendere l'esecuzione del codice da dove era stato interrotto
// (non si è più in VMX operation). Per tale motivo è necessario avere
// tutte le informazioni necessarie per riprendere la normale esecuzione
// del codice nel punto in cui era stata interrotta dalla VM exit che
// ha portato all'esecuzione di VMXOFF.
typedef struct _VMXOFF_STATE
{
	BOOLEAN IsVmxoffExecuted;			// Indica se VMXOFF è stata eseguita o meno.
	UINT64  GuestRip;					// Indirizzo a cui ritornare dopo VMXOFF.
	UINT64  GuestRsp;					// Stack pointer dopo VMXOFF.

} VMXOFF_STATE, * PVMXOFF_STATE;


// Risorse allocate nella virtualizzazione di ogni processore logico.
// Contiene anche lo stato del guest al momento della VMCALL che
// porta all'uscita dalla VMX operation con VMXOFF.
// Quest'ultima info serve perché VMXOFF è eseguita a seguito
// di una VMCALL, che causa una VM exit: dopo VMXOFF si è fuori dalla
// VMX operation e non si può usare VMRESUME per riprendere ad
// eseguire del codice. Per tale motivo è necessario salvare l'indirizzo
// di ritorno e lo stack pointer per riprendere dall'istruzione
// successiva alla vmcall che ha portato ad eseguire VMXOFF.
typedef struct _VCPU
{
	VMXOFF_STATE VmxoffState;	    // Stato del processore in previsione di VMXOFF
	UINT64 VMXON_REGION_VA;			// Indirizzo virtuale di regione VMXON
	UINT64 VMXON_REGION_PA;			// Indirizzo fisico di regione VMXON
	UINT64 VMCS_REGION_VA;			// Indirizzo virtuale di regione VMCS
	UINT64 VMCS_REGION_PA;			// Indirizzo fisico di regione VMCS
	UINT64 VMM_STACK;				// Stack dell'hypervisor durante VM-Exit
	UINT64 MSR_BITMAP_VA;           // Indirizzo virtuale di MSR Bitmap
	UINT64 MSR_BITMAP_PA;           // Indirizzo fisico di MSR Bitmap
	BOOLEAN IsOnVmxRootMode;		// Indica se il processore è in VMX root operation o meno
	BOOLEAN IncrementRip;			// Indica se eseguire l'istruzione guest successiva o ripetere la stessa dopo VMRESUME
}VCPU, * PVCPU;


// Variabile globale che punterà ad un array di VCPU.
// Dichiarata esterna così da evitare di inserire una definizione
// in un file header.
extern PVCPU GuestState;

// Salva le info nel caso non ci siano messaggi da inviare al Client
// al momento della registrazione di una sua richiesta.
PNOTIFY_RECORD GlobalNotifyRecord;