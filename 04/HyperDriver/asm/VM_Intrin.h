#pragma once

UINT16 __read_ldtr(void);
UINT16 __read_tr(void);
UINT16 __read_cs(void);
UINT16 __read_ss(void);
UINT16 __read_ds(void);
UINT16 __read_es(void);
UINT16 __read_fs(void);
UINT16 __read_gs(void);
VOID __sgdt(PVOID);
VOID __sidt(PVOID);
UINT32 __load_ar(UINT16);
BOOLEAN __vm_call(UINT64 vmcall_reason, UINT64 rdx, UINT64 r8, UINT64 r9);
BOOLEAN __vm_call_ex(UINT64 vmcall_reason, UINT64 rdx, UINT64 r8, UINT64 r9, UINT64 r10, UINT64 r11, UINT64 r12, UINT64 r13, UINT64 r14, UINT64 r15);
UINT64 __hyperv_vm_call(UINT64 param1, UINT64 param2, UINT64 param3);
VOID __reload_gdtr(UINT64 base, UINT32 limit);
VOID __reload_idtr(UINT64 base, UINT32 limit);
VOID __invept(UINT32 type, PVOID descriptors);
VOID __invvpid(UINT32 type, PVOID descriptors);
VOID __writecr2(UINT64 cr2);
INT __cdecl _rdseed16_step(PUINT16 return_value);
INT __cdecl _rdseed32_step(PUINT32 return_value);
INT __cdecl _rdseed64_step(PUINT64 return_value);