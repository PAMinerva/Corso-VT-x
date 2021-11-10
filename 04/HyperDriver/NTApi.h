#pragma once

NTKERNELAPI
_IRQL_requires_max_(APC_LEVEL)
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
KeGenericCallDpc(
	_In_ PKDEFERRED_ROUTINE Routine,
	_In_opt_ PVOID Context
);

NTKERNELAPI
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
KeSignalCallDpcDone(
	_In_ PVOID SystemArgument1
);

NTKERNELAPI
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
LOGICAL
KeSignalCallDpcSynchronize(
	_In_ PVOID SystemArgument2
);

typedef struct _NT_KPROCESS
{
	DISPATCHER_HEADER Header;
	LIST_ENTRY ProfileListHead;
	ULONG_PTR DirectoryTableBase;
	UCHAR Data[1];
}NT_KPROCESS, * PNT_KPROCESS;

// Ritorna indirizzo di Directory Table per il processo System.
// In altre parole è il valore di CR3 quando è in esecuzione System.
UINT64 FindSystemDirectoryTableBase();