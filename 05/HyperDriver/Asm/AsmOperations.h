#pragma once


extern VOID AsmVmExitHandler();
extern VOID inline AsmRestoreState();
extern VOID inline AsmSaveState();
extern NTSTATUS inline AsmVmxNonRootVmcall(ULONG64 VmcallNumber, ULONG64 OptionalParam1, ULONG64 OptionalParam2, LONG64 OptionalParam3);
extern UINT64 inline AsmHypervVmcall(ULONG64 HypercallInputValue, ULONG64 InputParamGPA, ULONG64 OutputParamGPA);