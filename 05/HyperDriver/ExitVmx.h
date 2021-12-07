#pragma once


// Esce da VMX operation
VOID TerminateVmx(UINT64 LogicalProcessors);

// Ritorna lo stack pointer da usare una volta usciti 
// dalla VMX operation tramite VMXOFF.
UINT64 ReturnRSPForVmxoff();

// Ritorna l'instruction pointer all'istruzione successiva
// da eseguire una volta usciti dalla VMX operation tramite
// VMXOFF.
UINT64 ReturnRIPForVmxoff();