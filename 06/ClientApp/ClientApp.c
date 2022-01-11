#include <Windows.h>
#include <stdio.h>

#include "Defs.h"

// Variabile sentinella che indica quando terminare il thread secondario.
BOOLEAN IsVmxExiting;


INT Error(const char* msg)
{
	printf("%s: error=%d\n", msg, GetLastError());
	return 1;
}


BOOL IsCPUIntel()
{
	CHAR CPUString[0x20];
	INT CPUInfo[4] = { -1 };
	UINT    nIds;

	// Usa l'intrinsics __cpuid dove valore da assegnare ad eax è passato come
	// secondo parametro mentre valori restituiti nei registri da CPUID vengono
	// salvati nel secondo parametro, che quindi è di output.
	 /*
		void __cpuid(
		int cpuInfo[4],
		int function_id
		);
	*/

	// In EAX viene restituito il massimo valore che CPUID riconosce e per cui
	// restituisce informazioni di base sulla CPU. Non ci interessa! 
	__cpuid(CPUInfo, 0);
	nIds = CPUInfo[0];

	// Riempie di 0 così carattere nullo terminatore già piazzato alla fine, qualunque essa sia.
	memset(CPUString, 0, sizeof(CPUString));

	// Se si monitora il valore degli elementi di CPUInfo in fase di debug si ottiene:
	// CPUInfo[1] = 1970169159 = 0x756E6547 = 'uneG' 
	// (0x75 = 'u'; 0x6E = 'n'; 0x65 = 'e'; 0x47 = 'G')
	// (0x47 = 'G' byte meno significativo; si ricordi che Intel usa ordine little-endian)
	// CPUInfo[2] = 1818588270 = 0x6C65746E = 'letn'
	// CPUInfo[3] = 1231384169 = 0x49656E69 = 'Ieni'
	*((PINT)CPUString) = CPUInfo[1];                           // EBX
	*((PINT)(CPUString + 4)) = CPUInfo[3];                     // EDX
	*((PINT)(CPUString + 8)) = CPUInfo[2];                     // ECX

	if (_stricmp(CPUString, "GenuineIntel") == 0) {
		return TRUE;
	}

	return FALSE;
}


void SendIrpToReadKernelMessage(HANDLE Device)
{
	BOOL    Status;
	ULONG   ReturnedLength;
	UINT32 OperationCode;

	printf("\n =============================== Kernel-Mode Logs (Driver) ===============================\n\n");

	// Alloca spazio per il buffer user mode in cui verranno copiati i messaggi del driver
	PCHAR OutputBuffer = (PCHAR)malloc(USER_MODE_BUFFER_SIZE);

	if (OutputBuffer == NULL)
	{
		printf("Insufficient memory available\n");
		return;
	}

	while (TRUE)
	{
		// Si continua ad inviare ciclicamente richieste al driver finché IsVmxExiting è FALSE.
		if (!IsVmxExiting)
		{
			ZeroMemory(OutputBuffer, USER_MODE_BUFFER_SIZE);

			Sleep(200);							// Piccola pausa per non stressare troppo la CPU

			// Crea e passa un IRP al driver per registrare la volontà del client di leggere un
			// messaggio.
			Status = DeviceIoControl(
				Device,							// Handle al device
				IOCTL_REGISTER_IRP,			    // IO Control code
				NULL,					        // Input buffer per il driver
				0,		                        // Dimensione in byte dell'input buffer
				OutputBuffer,					// Output buffer per il driver
				USER_MODE_BUFFER_SIZE,		    // Dimensione in byte dell'output buffer
				&ReturnedLength,				// Byte scritti nell'output buffer
				NULL							// Invocazione sincrona
			);

			if (!Status)
			{
				printf("Ioctl failed with code %d\n", GetLastError());
				break;
			}

			printf("\n========================= Kernel Mode (Buffer) =========================\n");

			// Legge il tipo di messaggio (info contenuta nei primi 32 bit del messaggio)
			OperationCode = 0;
			memcpy(&OperationCode, OutputBuffer, sizeof(UINT32));

			printf("Returned Length : 0x%x \n", ReturnedLength);
			printf("Operation Code : 0x%x \n", OperationCode);

			// Scrive il messaggio in base al tipo.
			switch (OperationCode)
			{
				case OPERATION_INFO_MESSAGE:
					printf("Information message :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;

				case OPERATION_WARNING_MESSAGE:
					printf("Warning message :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;

				case OPERATION_ERROR_MESSAGE:
					printf("Error message :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;

				case OPERATION_NON_IMMEDIATE_MESSAGE:
					printf("A buffer of messages :\n");
					printf("%s", OutputBuffer + sizeof(UINT32));
					break;

				default:
					break;
			}


			printf("========================================================================\n\n");

		}
		else
		{
			// Il thread viene terminato.
			return;
		}
	}
}


DWORD WINAPI ThreadFunc(void* Data)
{
	SendIrpToReadKernelMessage(Data);

	return 0;
}


int main()
{
	if (!IsCPUIntel())
	{
		printf("Sorry! Your need an Intel CPU.");
		return 0;
	}

	printf("Press Enter to enter VMX operation\n");
	getchar();

	// Non è possibile, per l'I\O Manager, gestire più operazioni di I\O
	// sincrone sullo stesso oggetto kernel. D'altra parte, non vogliamo 
	// che le richieste al driver, da parte del client, avvengono in 
	// maniera asincrona quindi si creerà l'handle al device con il flag 
	// FILE_FLAG_OVERLAPPED (per consentire la gestione di più richieste 
	// contemporaneamente) ma senza passare alcuna istanza di tipo OVERLAPPED 
	// nell'ultimo parametro di DeviceIoControl (per indicare di effetuare 
	// comunque le operazioni in maniera sincrona).
	// Per maggiori dettagli si veda:
	// https://community.osr.com/discussion/comment/134562/#Comment_134562
	HANDLE hDevice = CreateFile(
		L"\\\\.\\MyHypervisorDevice",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, /// lpSecurityAttirbutes
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		NULL); /// lpTemplateFile 

	if (hDevice == INVALID_HANDLE_VALUE)
	{
		return Error("Failed to open device");
	}

	// Crea il thread secondario che continua ad inviare richieste al driver.
	HANDLE Thread = CreateThread(NULL, 0, ThreadFunc, hDevice, 0, NULL);
	if (Thread) {
		printf("Ready to recieve messages from Kernel. Press Enter to exit VMX operation\n\n");
	}

	getchar();

	printf("[*] Terminating VMX !\n");

	// Indica che si sta per uscire dalla VMX operation.
	IsVmxExiting = TRUE;

	// Rihiede al driver di soddisfare eventuali IRP pendenti.
	BOOL Status = DeviceIoControl(
		hDevice,														// Handle al device
		IOCTL_RETURN_IRP_PENDING_PACKETS_AND_DISALLOW_IOCTL,			// IO Control code
		NULL,
		0,
		NULL,
		0,
		NULL,
		NULL
	);

	if (!Status) {
		printf("Ioctl failed with code %d\n", GetLastError());
	}

	CloseHandle(hDevice);

	printf("\nError : 0x%x\n", GetLastError());
	printf("[*] VMX operation disabled !\n");

	getchar();

	return 0;
}