#pragma once

//
// Indirizzi MSR
//

#define MSR_IA32_APIC_BASE                  0x0000001B
#define MSR_IA32_FEATURE_CONTROL            0x0000003A

#define MSR_IA32_VMX_BASIC                  0x00000480
#define MSR_IA32_VMX_PINBASED_CTLS          0x00000481
#define MSR_IA32_VMX_PROCBASED_CTLS         0x00000482
#define MSR_IA32_VMX_EXIT_CTLS              0x00000483
#define MSR_IA32_VMX_ENTRY_CTLS             0x00000484
#define MSR_IA32_VMX_MISC                   0x00000485
#define MSR_IA32_VMX_CR0_FIXED0             0x00000486
#define MSR_IA32_VMX_CR0_FIXED1             0x00000487
#define MSR_IA32_VMX_CR4_FIXED0             0x00000488
#define MSR_IA32_VMX_CR4_FIXED1             0x00000489
#define MSR_IA32_VMX_VMCS_ENUM              0x0000048A
#define MSR_IA32_VMX_PROCBASED_CTLS2        0x0000048B
#define MSR_IA32_VMX_EPT_VPID_CAP           0x0000048C
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS     0x0000048D
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS    0x0000048E
#define MSR_IA32_VMX_TRUE_EXIT_CTLS         0x0000048F
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS        0x00000490
#define MSR_IA32_VMX_VMFUNC                 0x00000491

#define MSR_IA32_SYSENTER_CS                0x00000174
#define MSR_IA32_SYSENTER_ESP               0x00000175
#define MSR_IA32_SYSENTER_EIP               0x00000176
#define MSR_IA32_DEBUGCTL                   0x000001D9

#define MSR_IA32_EFER                           0xC0000080
#define MSR_IA32_LSTAR                      0xC0000082

#define MSR_IA32_FS_BASE                    0xC0000100
#define MSR_IA32_GS_BASE                    0xC0000101
#define MSR_IA32_SHADOW_GS_BASE             0xC0000102


// Intervallo riservato ad Hyper-V per RDMSR e WRMSR
#define RESERVED_MSR_RANGE_LOW              0x40000000
#define RESERVED_MSR_RANGE_HI               0x400000F0



//
// Definizioni MSR
//

#pragma warning(disable : 4201)
typedef union _MSR
{
    struct
    {
        ULONG Low;
        ULONG High;
    };

    ULONG64 Uint64;
} MSR, * PMSR;
#pragma warning(default : 4201)

// MSR di controllo sulle funzionalità offerte dalla CPU
typedef union {
    struct {
        UINT64  Lock : 1;
        UINT64  EnableVmxInsideSmx : 1;
        UINT64  EnableVmxOutsideSmx : 1;
        UINT64  Reserved_3 : 5;
        UINT64  SenterLocalFunctionEnables : 7;
        UINT64  SenterGlobalEnable : 1;
        UINT64  Reserved_16 : 1;
        UINT64  SgxLaunchControlEnable : 1;
        UINT64  SgxEnable : 1;
        UINT64  Reserved_19 : 1;
        UINT64  LmceOn : 1;
        UINT64  Reserved_21 : 11;
        UINT64  Reserved_32 : 32;
    } Bits;
    UINT64  Uint64;
} IA32_FEATURE_CONTROL_MSR, * PIA32_FEATURE_CONTROL_MSR;


// MSR con informazioni di base per il supporto alla VMX operation
typedef union {
    struct {
        UINT32  VmcsRevisonId : 31;
        UINT32  MustBeZero : 1;
        UINT32  VmcsSize : 13;
        UINT32  Reserved_45 : 3;
        UINT32  VmcsAddressWidth : 1;
        UINT32  DualMonitor : 1;
        UINT32  MemoryType : 4;
        UINT32  InsOutsReporting : 1;
        UINT32  VmxTrueControls : 1;
        UINT32  Reserved_56 : 8;
    } Bits;
    UINT64  Uint64;
} IA32_VMX_BASIC_MSR, * PIA32_VMX_BASIC_MSR;