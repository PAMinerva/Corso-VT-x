#include <Windows.h>
#include <stdio.h>

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

int main()
{
	if (!IsCPUIntel())
	{
		printf("Sorry! Your need an Intel CPU.");
		return 0;
	}

	HANDLE hDevice = CreateFile(L"\\\\.\\MyHypervisorDevice", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		return Error("Failed to open device");
	}

	printf("Press Enter to exit\n\n");
	getchar();

	return 0;
}