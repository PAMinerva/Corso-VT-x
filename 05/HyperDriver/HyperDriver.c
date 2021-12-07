#include <ntddk.h>
#include "Globals.h"
#include "InitVmx.h"
#include "ExitVmx.h"


// Array di elementi con info utili per la virtualizzazione di ogni processore logico.
PVCPU GuestState = NULL;

NTSTATUS DriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	UINT64 ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	// Inizializza VMX, VMCS e lancia il guest
	if (InitializeVmx(ProcessorCount))
	{
		KdPrint(("[*] Hypervisor loaded successfully :)\n"));
	}
	else
	{
		KdPrint(("[*] Hypervisor was not loaded :("));
	}

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}


NTSTATUS DriverClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	UINT64 ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	// Esce da VMX operation
	TerminateVmx(ProcessorCount);

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

	// Libera lo spazio allocato per la virtualizzazione e
	// lo stato del guest sui vari processori logici
	if (GuestState)
		ExFreePoolWithTag((PUINT64)GuestState, POOLTAG);

	KdPrint(("[*] HyperDriver unloaded successfully.\n"));
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

	// Alloca spazio per tutti gli elementi necessari nell'array di VCPU.
	UINT64 ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	GuestState = ExAllocatePoolWithTag(NonPagedPool, sizeof(VCPU) * ProcessorCount, POOLTAG);
	if (!GuestState)
	{
		// Visualizza un avviso nell'output del debugger ed imposta un breakpoint.
		KdPrint(("Insufficient memory\n"));
		DbgBreakPoint();
		return FALSE;
	}

	RtlZeroMemory(GuestState, sizeof(VCPU) * ProcessorCount);

	return STATUS_SUCCESS;
}