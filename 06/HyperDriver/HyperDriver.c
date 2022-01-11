#include <ntddk.h>
#include "..\HyperDriver\Logging\Logging.h"
#include "Support\Globals.h"
#include "Vmx\InitVmx.h"
#include "Vmx\ExitVmx.h"
#include "..\HyperDriver\Logging\Defs.h"


// Array di elementi con info utili per la virtualizzazione di ogni processore logico.
PVCPU GuestState = NULL;

NTSTATUS DriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	// Il client è in esecuzione quindi si può dare il via al processamento
	// delle sue richieste.
	AllowIOCTLFromUsermode = TRUE;

	UINT64 ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

	// Inizializza VMX, VMCS e lancia il guest
	if (InitializeVmx(ProcessorCount))
	{
		LogInfo("Hypervisor loaded successfully :)");
	}
	else
	{
		LogInfo("Hypervisor was not loaded :(");
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

	// Esce dalla VMX operation
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

	KdPrint(("[*] Uinitializing logs\n"));
	LogUnInitialize();

	// Libera lo spazio allocato per la virtualizzazione e
	// lo stato del guest sui vari processori logici
	if (GuestState)
		ExFreePoolWithTag((PUINT64)GuestState, POOLTAG);

	KdPrint(("[*] HyperDriver unloaded successfully.\n"));
}


NTSTATUS DriverDispatchIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION  IrpStack;
	NTSTATUS    Status;

	// AllowIOCTLFromUsermode TRUE finché il client non invoca DeviceIoControl con
	// l'IO Control code IOCTL_RETURN_IRP_PENDING_PACKETS_AND_DISALLOW_IOCTL.
	if (AllowIOCTLFromUsermode)
	{
		IrpStack = IoGetCurrentIrpStackLocation(Irp);

		switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
		{
		case IOCTL_REGISTER_IRP:
			Status = LogRegisterIrpBasedNotification(DeviceObject, Irp);
			break;

		case IOCTL_RETURN_IRP_PENDING_PACKETS_AND_DISALLOW_IOCTL:
			// Indica che si sta uscendo dalla VMX operation e quindi non si gestiranno più 
			// più richieste dal client tramite IOCTL.
			AllowIOCTLFromUsermode = FALSE;
			// Invia un messaggio all'immediate buffer per soddisfare la richiesta correntemente
			// pendente del client. Il relativo messaggio viene messo nell'immediate buffer per
			// indicare che il client non invierà più richieste quindi per leggere eventuali 
			// messaggi rimasti nel non-immediate buffer è necessario rieseguire il client.
			LogInfoImmediate("An immediate message recieved, we no longer recieve IRPs from user-mode");
			Status = STATUS_SUCCESS;
			break;

		default:
			ASSERT(FALSE);  // Non si dovrebbe arrivare mai qui.
			Status = STATUS_NOT_IMPLEMENTED;
			break;
		}
	}
	else
	{
		// Il fatto che AllowIOCTLFromUsermode sia FALSE non esclude che il thread
		// secondario del client possa inviare un'ulteriore richiesta.
		// In quel caso si restituisce semplicemente STATUS_SUCCESS per indicare
		// di archiviare tale richiesta come soddisfatta.
		Status = STATUS_SUCCESS;
	}

	if (Status != STATUS_PENDING) {
		Irp->IoStatus.Status = Status;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}

	return Status;
}


NTSTATUS DriverUnsupported(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	LogWarning("This function is not supported :(");

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}


NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	if (!LogInitialize())
	{
		KdPrint(("[*] Log buffer is not initialized !\n"));
		DbgBreakPoint();
	}

	for (unsigned int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = DriverUnsupported;

	// Imposta funzione chiamata quando driver verrà stoppato.
	// Si occuperà di cancellare il device ed il symbolic link creati in DriverEntry.
	DriverObject->DriverUnload = DriverUnload;

	// Invocate quando in usermode vengono chiamate CreateFile e CloseHandle
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverClose;

	// Invocata quando in usermode si usa DeviceIoControl.
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDispatchIoControl;

	// Istruzione seguente equivale a 
	// RtlInitUnicodeString(&devName, L"\\Device\\MyDevice");
	// ma è più efficiente in quanto RtlInitUnicodeString calcola la lunghezza da inserire in UNICODE_STRING.Lenght a runtime
	// mentre la macro RTL_CONSTANT_STRING lo fa a compile-time
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\MyHypervisorDevice");

	// Crea device object
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		LogError("Failed to create device (0x%08X)", status);
		return status;
	}

	// Crea symbolic link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MyHypervisorDevice");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		LogError("Failed to create symbolic link (0x%08X)", status);
		IoDeleteDevice(DeviceObject);
		return status;
	}

	// Alloca spazio per tutti gli elementi necessari nell'array di VCPU.
	UINT64 ProcessorCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	GuestState = ExAllocatePoolWithTag(NonPagedPool, sizeof(VCPU) * ProcessorCount, POOLTAG);
	if (!GuestState)
	{
		// Visualizza un avviso nell'output del debugger ed imposta un breakpoint.
		LogError("Insufficient memory");
		return FALSE;
	}

	RtlZeroMemory(GuestState, sizeof(VCPU) * ProcessorCount);

	return STATUS_SUCCESS;
}