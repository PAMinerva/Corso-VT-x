#include <ntddk.h>
#include <intrin.h>
#include "Msr.h"
#include "Globals.h"
#include "InitVmx.h"
#include "Segment.h"
#include "Asm\AsmOperations.h"
#include "Asm\VmIntrinsics.h"
#include "Vmcs.h"
#include "NTApi.h"


/// Carica i campi relativi alla segmentazione recuperandoli dal segment descriptor
VOID FillGuestSegmentData(PVOID gdt_base, UINT32 segment_register, UINT16 selector)
{
	SEGMENT_ATTRIBUTES segment_access_rights;
	PSEGMENT_DESCRIPTOR segment_descriptor;

	// Se il bit 2 del selector è impostato ad 1 significa che tale selector 
	// si riferisce ad elementi all'interno della LDT (non ci interessa).
	if (selector & 0x4)
		return;

	// L'indice del segment descriptor (all'interno della GDT) si trova in 
	// bit 3-15 del selector e viene moltiplicato per 8 (i byte dei descriptor nella GDT) 
	// per calcolare il byte offset del descriptor a partire dall'inizio della GDT.
	// Moltiplicare per 8 equivale a shiftare a sinistra di 3 posizioni
	// (cioè gli ultimi 3 bit saranno zero). Dato che negli ultimi 3 bit del
	// selector c'è roba che non ci interessa si possono azzerare ed è come
	// se avessimo moltiplicato per 8 (0x7 = 0111; ~0x7 = 1000).
	segment_descriptor = (PSEGMENT_DESCRIPTOR)((PCHAR)gdt_base + (selector & ~0x7));

	// Base del segmento si compone attraverso i bit 16-31 (low), 32-39 (mid) e 
	// 56-63 (high) del segment descriptor.
	UINT64 segment_base = segment_descriptor->Bits.BaseLow |
		segment_descriptor->Bits.BaseMid << 16 |
		segment_descriptor->Bits.BaseHigh << 24;

	// Limite del segmento si compone attraverso i bit 0-15 (low) e 
	// 48-51 (high) del segment descriptor.
	UINT32 segment_limit = segment_descriptor->Bits.LimitLow | (segment_descriptor->Bits.LimitHigh << 16);

	// __load_ar usa l'istruzione LAR per leggere e ritornare i bit 32-63
	// del segment descriptor a cui si riferisce il selector passato come argomento.
	// LAR serve a ritornare gli attributi di accesso di un segmento, ma i 
	// primi 8 bit (32-39) vengono azzerati perché contengono la BaseMid, che
	// non fa parte degli attributi, quindi è necessario shiftare.
	segment_access_rights.Uint32 = __load_ar(selector) >> 8;
	segment_access_rights.Bits.Unusable = 0;
	segment_access_rights.Bits.Reserved_17 = 0;

	// Se il bit relativo al flag S è impostato a 0 allora si tratta di descriptor 
	// di sistema. In sistemi a 64 bit tali descriptor sono di 16 byte e per 
	// comporre la base del segmento è necessario aggiungere anche i bit 95:64 
	// (i bit 96:127 sono riservati) come 32 bit alti di un indirizzo a 64 bit.
	// Dato che questo codice è eseguito in modalità a 64 bit non c'è pericolo
	// di incontrare un segment descriptor di sistema che sia di 8 byte.
	if (segment_descriptor->Bits.S == FALSE)
		segment_base = (segment_base & 0xFFFFFFFF) | (UINT64)segment_descriptor->Bits.BaseUpper << 32;

	// Se il bit della granularità è impostato ad 1 il limite del segmento
	// non è più inteso in byte ma in unità di 4 KB.
	if (segment_descriptor->Bits.G == TRUE)
		segment_limit = (segment_limit << 12) + 0xfff;

	// Se il selector è nullo si imposta ad 1 il bit Unusable degli attributi di accesso.
	if (selector == 0)
		segment_access_rights.Uint32 |= 0x10000;

	// Al valore codificato GUEST_ES_SELECTOR seguono GUEST_CS_SELECTOR, GUEST_SS_SELECTOR, ecc.
	// e la distanza numerica tra loro è sempre di 2 unità. 
	// Stessa cosa per tutti gli altri valori codificati GUEST_ES_XX.
	// Quindi, per indicare tutti questi valori codificati è sufficiente usare un indice 
	// (in questo caso segment_register) da moltiplicare per 2 ed aggiungere il risultato 
	// a GUEST_ES_XX.
	__vmx_vmwrite(GUEST_ES_SELECTOR + segment_register * 2, selector);
	__vmx_vmwrite(GUEST_ES_LIMIT + segment_register * 2, segment_limit);
	__vmx_vmwrite(GUEST_ES_BASE + segment_register * 2, segment_base);
	__vmx_vmwrite(GUEST_ES_ACCESS_RIGHTS + segment_register * 2, segment_access_rights.Uint32);
}

void SetVmEntryCtls(PVM_ENTRY_CONTROL entry_control)
{
	/**
	* This control determines whether DR7 and the IA32_DEBUGCTL MSR are loaded on VM entry.
	* The first processors to support the virtual-machine extensions supported only the 1-setting of
	* this control.
	*/
	entry_control->Bits.LoadDebugControls = TRUE;

	/**
	* On processors that support Intel 64 architecture, this control determines whether the logical
	* processor is in IA-32e mode after VM entry. Its value is loaded into IA32_EFER.LMA as part of
	* VM entry.
	* This control must be 0 on processors that do not support Intel 64 architecture.
	*/
	entry_control->Bits.IA32eModeGuest = TRUE;

	/**
	* If this control is 1, Intel Processor Trace does not produce a paging information packet (PIP) on
	* a VM entry or a VMCS packet on a VM entry that returns from SMM (see Chapter 35).
	*/
	entry_control->Bits.ConcealVMXFromPT = TRUE;
}

void SetVmExitCtls(PVM_EXIT_CONTROL exit_control)
{
	/**
	* This control determines whether DR7 and the IA32_DEBUGCTL MSR are saved on VM exit.
	* The first processors to support the virtual-machine extensions supported only the 1-
	* setting of this control.
	*/
	exit_control->Bits.SaveDebugControls = TRUE;

	/**
	* On processors that support Intel 64 architecture, this control determines whether a logical
	* processor is in 64-bit mode after the next VM exit. Its value is loaded into CS.L,
	* IA32_EFER.LME, and IA32_EFER.LMA on every VM exit.
	* This control must be 0 on processors that do not support Intel 64 architecture.
	*/
	exit_control->Bits.HostAddressSpaceSize = TRUE;

	/**
	* This control affects VM exits due to external interrupts:
	* • If such a VM exit occurs and this control is 1, the logical processor acknowledges the
	*   interrupt controller, acquiring the interrupt’s vector. The vector is stored in the VM-exit
	*   interruption-information field, which is marked valid.
	* • If such a VM exit occurs and this control is 0, the interrupt is not acknowledged and the
	*   VM-exit interruption-information field is marked invalid.
	*/
	exit_control->Bits.AcknowledgeIntOnExit = TRUE;

	/**
	* If this control is 1, Intel Processor Trace does not produce a paging information packet (PIP)
	* on a VM exit or a VMCS packet on an SMM VM exit (see Chapter 35).
	*/
	exit_control->Bits.ConcealVMXFromPT = TRUE;
}

void SetPrimaryCtls(PPRIMARY_PROCBASED_CTLS primary_controls)
{
	/**
	* This control determines whether MSR bitmaps are used to control execution of the RDMSR
	* and WRMSR instructions (see Section 24.6.9 and Section 25.1.3).
	* For this control, “0” means “do not use MSR bitmaps” and “1” means “use MSR bitmaps.” If the
	* MSR bitmaps are not used, all executions of the RDMSR and WRMSR instructions cause
	* VM exits.
	*/
	primary_controls->Bits.UseMSRBitmaps = TRUE;

	/**
	* This control determines whether the secondary processor-based VM-execution controls are
	* used. If this control is 0, the logical processor operates as if all the secondary processor-based
	* VM-execution controls were also 0.
	*/
	primary_controls->Bits.ActivateSecondaryControls = TRUE;

	/**
	* This control determines whether executions of HLT cause VM exits.
	*/
	//primary_controls->Bits.HLTExiting = TRUE;
}

void SetSecondaryCtls(PSECONDARY_PROCBASED_CTLS secondary_controls)
{
	/**
	* If this control is 1, extended page tables (EPT) are enabled. See Section 28.2.
	*/
	//secondary_controls->Bits.EnableEPT = TRUE;

	/**
	* If this control is 0, any execution of RDTSCP causes an invalid-opcode exception (#UD).
	*/
	secondary_controls->Bits.EnableRDTSCP = TRUE;

	/**
	* If this control is 1, cached translations of linear addresses are associated with a virtual-
	* processor identifier (VPID). See Section 28.1.
	*/
	//secondary_controls->Bits.EnableVPID = TRUE;

	/**
	* If this control is 0, any execution of INVPCID causes a #UD.
	*/
	secondary_controls->Bits.EnableINVPCID = TRUE;

	/**
	* If this control is 1, Intel Processor Trace suppresses from PIPs an indication that the processor
	* was in VMX non-root operation and omits a VMCS packet from any PSB+ produced in VMX non-
	* root operation (see Chapter 35).
	*/
	secondary_controls->Bits.ConcealVMXFromPT = TRUE;

	/**
	* If this control is 0, any execution of XSAVES or XRSTORS causes a #UD.
	*/
	secondary_controls->Bits.EnableXSAVESAndXRSTORS = TRUE;
}

UINT64 GetSegmentBase(UINT16 selector, PUINT8 gdt_base)
{
	PSEGMENT_DESCRIPTOR segment_descriptor;

	// Recupera segment descriptor dalla GDT.
	// L'indice del segment descriptor si trova a partire dal bit 3 del selector
	// e viene moltiplicato per 8 (la dimensione dei descriptor nella GDT) per
	// calcolare il byte offset del descriptor a partire dalla base della GDT.
	// Moltiplicare per 8 è equivalente a shiftare a sinistra di 3 posizioni
	// (cioè gli ultimi 3 bit saranno zero). Dato che negli ultimi 3 bit del
	// selector c'è roba che non ci interessa si possono azzerare ed è come
	// se avessimo moltiplicato per 8 (0x7 = 0111; ~0x7 = 1000).
	segment_descriptor = (PSEGMENT_DESCRIPTOR)(gdt_base + (selector & ~0x7));

	// Calcola base del segmento.
	// Base del segmento si compone attraverso i bit 16-31 (low), 32-39 (mid) e 
	// 56-63 (high) del segment descriptor.
	unsigned __int64 segment_base = segment_descriptor->Bits.BaseLow |
		segment_descriptor->Bits.BaseMid << 16 | segment_descriptor->Bits.BaseHigh << 24;

	// Se il bit relativo al flag S è impostato a 0 allora si tratta di descriptor
	//  di sistema. In x64 tali descriptor sono di 16 byte e per comporre la base 
	// è necessario aggiungere anche i bit 64-95 (i bit 96-127 sono riservati) 
	// come 32 bit alti di un indirizzo a 64 bit.
	if (segment_descriptor->Bits.S == FALSE)
		segment_base = (segment_base & 0xFFFFFFFF) | (UINT64)segment_descriptor->Bits.BaseUpper << 32;

	return segment_base;
}

UINT32 AdjustControls(UINT32 ctl, UINT32 msr)
{
	MSR msr_value = { 0 };
	msr_value.Uint64 = __readmsr(msr);
	ctl &= msr_value.High;
	ctl |= msr_value.Low;
	return ctl;
}

// Imposta a clear lo stato della VMCS e svuota la cache usando Vmclear.
BOOLEAN ClearVmcs(PVCPU CurrentGuestState)
{
	// La VMCS passata come parametro diventa inattiva.
	int VmclearStatus = __vmx_vmclear(&CurrentGuestState->VMCS_REGION_PA);

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
BOOLEAN LoadVmcs(PVCPU CurrentGuestState) {

	int VmptrldStatus = __vmx_vmptrld(&CurrentGuestState->VMCS_REGION_PA);

	KdPrint(("[*] Vmcs Vmptrld Status : %d\n", VmptrldStatus));

	if (VmptrldStatus)
	{
		KdPrint(("[*] VMCS failed to load ( status : %d )", VmptrldStatus));
		return FALSE;
	}
	return TRUE;
}

VOID SetupVmcs(PVCPU CurrentGuestState, PVOID GuestStack)
{
	// Istruzioni SGDT e SIDT restituiscono i registri GDTR e IDTR.
	// In realtà restituiscono pseudo descriptor che indicano
	// indirizzo di base e limite della GDT e della IDT.
	PSEUDO_DESCRIPTOR64 gdtr = { 0 };
	PSEUDO_DESCRIPTOR64 idtr = { 0 };

	// I selector dell'host devono avere gli ultimi 3 bit azzerati
	UINT8 selector_mask = 7;

	// Campi di controllo della VMCS
	PINBASED_CTLS pinbased_controls = { 0 };
	PRIMARY_PROCBASED_CTLS primary_controls = { 0 };
	SECONDARY_PROCBASED_CTLS secondary_controls = { 0 };
	VM_EXIT_CONTROL exit_controls = { 0 };
	VM_ENTRY_CONTROL entry_controls = { 0 };


	// Imposta a clear lo stato della VMCS.
	if (!ClearVmcs(CurrentGuestState))
	{
		DbgBreakPoint();
		return;
	}

	// Imposta la VMCS come corrente ed attiva.
	if (!LoadVmcs(CurrentGuestState))
	{
		DbgBreakPoint();
		return;
	}


	// Imposta bit specifici nei campi di controllo.
	SetPrimaryCtls(&primary_controls);
	SetSecondaryCtls(&secondary_controls);
	SetVmExitCtls(&exit_controls);
	SetVmEntryCtls(&entry_controls);



	// Legge MSR IA32_VMX_BASIC
	IA32_VMX_BASIC_MSR vmx_basic = { 0 };
	vmx_basic.Uint64 = __readmsr(MSR_IA32_VMX_BASIC);


	// Legge GDTR e IDTR
	__sgdt(&gdtr);
	__sidt(&idtr);



	//
	// Host Area
	//

	// Registri di controllo
	__vmx_vmwrite(HOST_CR0, __readcr0());
	__vmx_vmwrite(HOST_CR3, FindSystemDirectoryTableBase());
	__vmx_vmwrite(HOST_CR4, __readcr4());

	// Registri general purpose (RSP e RIP)
	__vmx_vmwrite(HOST_RSP, CurrentGuestState->VMM_STACK + VMM_STACK_SIZE - 16);
	__vmx_vmwrite(HOST_RIP, (UINT64)AsmVmExitHandler);

	// Selettori dei registri di segmento
	__vmx_vmwrite(HOST_CS_SELECTOR, __read_cs() & ~selector_mask);
	__vmx_vmwrite(HOST_SS_SELECTOR, __read_ss() & ~selector_mask);
	__vmx_vmwrite(HOST_DS_SELECTOR, __read_ds() & ~selector_mask);
	__vmx_vmwrite(HOST_ES_SELECTOR, __read_es() & ~selector_mask);
	__vmx_vmwrite(HOST_FS_SELECTOR, __read_fs() & ~selector_mask);
	__vmx_vmwrite(HOST_GS_SELECTOR, __read_gs() & ~selector_mask);
	__vmx_vmwrite(HOST_TR_SELECTOR, __read_tr() & ~selector_mask);

	// Campo Base che si trova nei registri GDTR, IDTR, TR, FS e GS
	__vmx_vmwrite(HOST_GDTR_BASE, gdtr.base_address);
	__vmx_vmwrite(HOST_IDTR_BASE, idtr.base_address);
	__vmx_vmwrite(HOST_FS_BASE, __readmsr(MSR_IA32_FS_BASE));
	__vmx_vmwrite(HOST_GS_BASE, __readmsr(MSR_IA32_GS_BASE));
	__vmx_vmwrite(HOST_TR_BASE, GetSegmentBase(__read_tr(), (PUINT8)gdtr.base_address));

	// MSR (SYSENTER/SYSEXIT)
	__vmx_vmwrite(HOST_SYSENTER_CS, __readmsr(MSR_IA32_SYSENTER_CS));
	__vmx_vmwrite(HOST_SYSENTER_ESP, __readmsr(MSR_IA32_SYSENTER_ESP));
	__vmx_vmwrite(HOST_SYSENTER_EIP, __readmsr(MSR_IA32_SYSENTER_EIP));



	//
	// Guest Area
	//

		// Registri di segmento
	FillGuestSegmentData((PVOID)gdtr.base_address, ES, __read_es());
	FillGuestSegmentData((PVOID)gdtr.base_address, CS, __read_cs());
	FillGuestSegmentData((PVOID)gdtr.base_address, SS, __read_ss());
	FillGuestSegmentData((PVOID)gdtr.base_address, DS, __read_ds());
	FillGuestSegmentData((PVOID)gdtr.base_address, FS, __read_fs());
	FillGuestSegmentData((PVOID)gdtr.base_address, GS, __read_gs());
	FillGuestSegmentData((PVOID)gdtr.base_address, LDTR, __read_ldtr());
	FillGuestSegmentData((PVOID)gdtr.base_address, TR, __read_tr());

	// Registri di controllo
	__vmx_vmwrite(GUEST_CR0, __readcr0());
	__vmx_vmwrite(GUEST_CR3, __readcr3());
	__vmx_vmwrite(GUEST_CR4, __readcr4());

	// Registro di debug
	__vmx_vmwrite(GUEST_DR7, __readdr(7));

	// Registri general purpose (RSP e RIP) ed RFLAGS
	__vmx_vmwrite(GUEST_RSP, (UINT64)GuestStack);
	__vmx_vmwrite(GUEST_RIP, (ULONG64)AsmRestoreState);
	__vmx_vmwrite(GUEST_RFLAGS, __readeflags());

	// GDTR ed IDTR (base e limit)
	__vmx_vmwrite(GUEST_GDTR_BASE, gdtr.base_address);
	__vmx_vmwrite(GUEST_GDTR_LIMIT, gdtr.limit);
	__vmx_vmwrite(GUEST_IDTR_BASE, idtr.base_address);
	__vmx_vmwrite(GUEST_IDTR_LIMIT, idtr.limit);

	// MSR
	__vmx_vmwrite(GUEST_DEBUG_CONTROL, __readmsr(MSR_IA32_DEBUGCTL));
	__vmx_vmwrite(GUEST_SYSENTER_CS, __readmsr(MSR_IA32_SYSENTER_CS));
	__vmx_vmwrite(GUEST_SYSENTER_ESP, __readmsr(MSR_IA32_SYSENTER_ESP));
	__vmx_vmwrite(GUEST_SYSENTER_EIP, __readmsr(MSR_IA32_SYSENTER_EIP));
	__vmx_vmwrite(GUEST_FS_BASE, __readmsr(MSR_IA32_FS_BASE));
	__vmx_vmwrite(GUEST_GS_BASE, __readmsr(MSR_IA32_GS_BASE));

	// VMCS Link Pointer
	__vmx_vmwrite(GUEST_VMCS_LINK_POINTER, ~0ULL);



	//
	// Campi di controllo
	//

	// VM-execution, VM-exit e VM-entry control
	__vmx_vmwrite(CONTROL_PIN_BASED_VM_EXECUTION_CONTROLS,
		AdjustControls(pinbased_controls.Uint32,
			vmx_basic.Bits.VmxTrueControls ? MSR_IA32_VMX_TRUE_PINBASED_CTLS : MSR_IA32_VMX_PINBASED_CTLS));

	__vmx_vmwrite(CONTROL_PRIMARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
		AdjustControls(primary_controls.Uint32,
			vmx_basic.Bits.VmxTrueControls ? MSR_IA32_VMX_TRUE_PROCBASED_CTLS : MSR_IA32_VMX_PROCBASED_CTLS));

	__vmx_vmwrite(CONTROL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
		AdjustControls(secondary_controls.Uint32, MSR_IA32_VMX_PROCBASED_CTLS2));

	__vmx_vmwrite(CONTROL_VM_EXIT_CONTROLS,
		AdjustControls(exit_controls.Uint32,
			vmx_basic.Bits.VmxTrueControls ? MSR_IA32_VMX_TRUE_EXIT_CTLS : MSR_IA32_VMX_EXIT_CTLS));

	__vmx_vmwrite(CONTROL_VM_ENTRY_CONTROLS,
		AdjustControls(entry_controls.Uint32,
			vmx_basic.Bits.VmxTrueControls ? MSR_IA32_VMX_TRUE_ENTRY_CTLS : MSR_IA32_VMX_ENTRY_CTLS));


	// Per evitare di causare VM exit ogni volta che viene eseguita una RDMSR o una WRMSR
	// è meglio impostare una bitmap con bit impostati ad 1 per i soli MSR per cui una
	// VM exit è l'azione desiderata a seguito di un accesso in letture o scrittura.
	if (primary_controls.Bits.UseMSRBitmaps == TRUE)
		__vmx_vmwrite(CONTROL_MSR_BITMAPS_ADDRESS, CurrentGuestState->MSR_BITMAP_PA);


	// Imposta la maschera di CR4 in modo che il bit 13 (VMXE) appartenga all'host
	// (quando il guest legge CR4 il bit 13 esposto al guest sarà quello in CR4 shadow).
	__vmx_vmwrite(CONTROL_CR4_GUEST_HOST_MASK, 0x2000);

	// Imposta valore di CR4 shadow.
	// ~0x2000 equivale a tutti i bit ad 1 tranne il 13-esimo, che è zero.
	// Quindi se il guest legge CR4.VMXE gli verrà ritornato 0 invece che 1.
	__vmx_vmwrite(CONTROL_CR4_READ_SHADOW, __readcr4() & ~0x2000);
}