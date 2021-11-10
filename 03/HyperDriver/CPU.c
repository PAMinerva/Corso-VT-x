#include <ntddk.h>
#include "CPU.h"
#include "MSR.h"

BOOLEAN CpuIsVmxSupported()
{
	CPUID_EAX_01 data = { 0 };

	// Check VMX bit (il quinti bit in ecx)
	// La struct CPUID_EAX_1 definita con 4 campi interi contigui 
	// quindi può essere trattata come fosse un array.
	__cpuid((PINT32)&data, 1);

	if (!_bittest((PLONG)&data.ecx, 5))   //if ((data.ecx & (1 << 5)) == 0)
		return FALSE;

	// Usa intrinsics __readmsr per leggere MSR IA32_FEATURE_CONTROL
	IA32_FEATURE_CONTROL_MSR Control = { 0 };
	Control.Uint64 = __readmsr(MSR_IA32_FEATURE_CONTROL);

	// Controlla bit 2 di IA32_FEATURE_CONTROL.
	// Se non è impostato ad 1 vuol dire che la virtualizzazione
	// è disabilitata dal BIOS e bisogna attivarla.
	if (Control.Bits.EnableVmxOutsideSmx == FALSE)
	{
		DbgPrint("Please enable Virtualization from BIOS");
		return FALSE;
	}

	return TRUE;
}

VOID CpuEnableVMX()
{
	CR4 Register;

	// Recupera registro CR4
	Register.Uint64 = __readcr4();

	// Cambia ad 1 il bit 13 (VMX Enable)
	Register.Bits.VMXE = 1;

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