#pragma once

//
// Indirizzi MSR
//

#define MSR_IA32_FEATURE_CONTROL         0x0000003A

#define MSR_IA32_VMX_CR0_FIXED0          0x00000486
#define MSR_IA32_VMX_CR0_FIXED1          0x00000487
#define MSR_IA32_VMX_CR4_FIXED0          0x00000488
#define MSR_IA32_VMX_CR4_FIXED1          0x00000489


//
// Definizioni di MSR
//

// MSR di controllo sulle funzionalità offerte dalla CPU
typedef union {
    struct {
        UINT64  Lock : 1;
        UINT64  EnableVmxInsideSmx : 1;
        UINT64  EnableVmxOutsideSmx : 1;
        UINT64  Reserved1 : 5;
        UINT64  SenterLocalFunctionEnables : 7;
        UINT64  SenterGlobalEnable : 1;
        UINT64  Reserved2 : 1;
        UINT64  SgxLaunchControlEnable : 1;
        UINT64  SgxEnable : 1;
        UINT64  Reserved3 : 1;
        UINT64  LmceOn : 1;
        UINT64  Reserved4 : 11;
        UINT64  Reserved5 : 32;
    } Bits;
    UINT64  Uint64;
} IA32_FEATURE_CONTROL_MSR, * PIA32_FEATURE_CONTROL_MSR;