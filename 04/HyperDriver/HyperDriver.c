#include <ntddk.h>
#include "CPU.h"
#include "VMX.h"
#include "Globals.h"
#include "Memory.h"
#include "VMCS.h"

PVCPU vmState = NULL;

NTSTATUS DriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS DriverClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

void DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	// Cancella symbolic link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyHypervisorDevice");
	IoDeleteSymbolicLink(&symLink);

	// Cancella device object
	IoDeleteDevice(DriverObject->DeviceObject);

	UINT64 ProcessorCount = 1;
	ULONG ProcessorIndex = KeGetCurrentProcessorIndex();
	VmxTerminate(ProcessorCount, ProcessorIndex);
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	// Imposta funzione chiamata quando driver verrà stoppato.
	// Si occuperà di cancellare il device ed il symbolic link creati in DriverEntry.
	DriverObject->DriverUnload = DriverUnload;

	// Invocate quando in usermode vengono chiamate CreateFile e CloseHandle
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverClose;

	// Istruzione seguente equivale a 
	// RtlInitUnicodeString(&devName, L"\\Device\\MyDevice");
	// ma è più efficiente in quanto RtlInitUnicodeString calcola la lunghezza da inserire in UNICODE_STRING.Lenght a runtime
	// mentre la macro RTL_CONSTANT_STRING lo fa a compile-time
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\MyHypervisorDevice");

	// Crea device object
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create device (0x%08X)\n", status));
		return status;
	}

	// Crea symbolic link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyHypervisorDevice");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(DeviceObject);
		return status;
	}

	// Lo scopo di questo esempio è quello di eseguire un HLT ed uscire dalla 
	// VMX operation.
	// Dato che non si è ancora virtualizzato l'intero sistema in uso, si può 
	// usare un solo processore logico che, una volta uscito dalla VMX operation,
	// può ritorna qui, per continuare ad eseguire il codice di DriveEntry.
	// Se si volessero usare tutti i processori, cosa eseguirebbero questi una
	// volta usciti dalla VMX operation se solo ad uno di essi è permesso di
	// eseguire DriverEntry? Si ragioni su questo.
	UINT64 ProcessorCount = 1; // KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	ULONG ProcessorIndex = KeGetCurrentProcessorIndex();

	// Alloca spazio per un solo VCPU nella variabile globale che conserva le
	// risorse da allocare per la virtualizzazione di ogni processore logico.
	vmState = ExAllocatePoolWithTag(NonPagedPool, sizeof(VCPU) * ProcessorCount, POOLTAG);
	RtlZeroMemory(vmState, sizeof(VCPU) * ProcessorCount);


	if (VmxInitialize(ProcessorCount, ProcessorIndex))
	{
		KdPrint(("[*] Hypervisor loaded successfully :)\n"));

		KdPrint(("[*] Restoring old thread affinity.\n"));
		KeRevertToUserGroupAffinityThread(&(vmState->OldAffinity));

		KdPrint(("[*] Clearing CR4.VMXE bit.\n"));
		CpuVmxEnable(FALSE);
	}
	else
	{
		KdPrint(("[*] Hypervisor was not loaded :("));

		KdPrint(("[*] Restoring old thread affinity.\n"));
		KeRevertToUserGroupAffinityThread(&(vmState->OldAffinity));

		KdPrint(("[*] Clearing CR4.VMXE bit.\n"));
		CpuVmxEnable(FALSE);
	}

	return STATUS_SUCCESS;
}