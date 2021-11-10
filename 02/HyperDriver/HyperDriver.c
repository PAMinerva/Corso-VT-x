#include <ntddk.h>
#include "CPU.h"

NTSTATUS DriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	if (CpuIsVmxSupported())
	{
		// CpuEnableVMX(); // inutile farlo in modo isolato
		CpuFixBits();
		KdPrint(("VMX Operation Enabled Successfully !\n"));
	}

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

	KdPrint(("HypertDriver initialized successfully\n"));

	return STATUS_SUCCESS;
}