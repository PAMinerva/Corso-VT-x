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
VOID __reload_gdtr(ULONG64 base, ULONG limit);
VOID __reload_idtr(ULONG64 base, ULONG limit);