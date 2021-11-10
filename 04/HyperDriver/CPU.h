#pragma once

// Registri utili in istruzione CPUID quando EAX == 0 o EAX == 1
typedef struct _CPUID_EAX_01
{
    INT eax;
    INT ebx;
    INT ecx;
    INT edx;
} CPUID_EAX_01, * PCPUID_EAX_01;

// Control Register 0 (CR0).
typedef union {
    struct {
        UINT64  PE : 1;           ///< Protection Enable.
        UINT32  MP : 1;           ///< Monitor Coprocessor.
        UINT32  EM : 1;           ///< Emulation.
        UINT32  TS : 1;           ///< Task Switched.
        UINT32  ET : 1;           ///< Extension Type.
        UINT32  NE : 1;           ///< Numeric Error.
        UINT32  Reserved_0 : 10;  ///< Reserved.
        UINT32  WP : 1;           ///< Write Protect.
        UINT32  Reserved_1 : 1;   ///< Reserved.
        UINT32  AM : 1;           ///< Alignment Mask.
        UINT32  Reserved_2 : 10;  ///< Reserved.
        UINT32  NW : 1;           ///< Mot Write-through.
        UINT32  CD : 1;           ///< Cache Disable.
        UINT32  PG : 1;           ///< Paging.
    } Bits;
    UINT64     Uint64;
} CR0, * PCR0;

// Control Register 4 (CR4).
typedef union {
    struct {
        UINT32  VME : 1;          ///< Virtual-8086 Mode Extensions.
        UINT32  PVI : 1;          ///< Protected-Mode Virtual Interrupts.
        UINT32  TSD : 1;          ///< Time Stamp Disable.
        UINT32  DE : 1;           ///< Debugging Extensions.
        UINT32  PSE : 1;          ///< Page Size Extensions.
        UINT32  PAE : 1;          ///< Physical Address Extension.
        UINT32  MCE : 1;          ///< Machine Check Enable.
        UINT32  PGE : 1;          ///< Page Global Enable.
        UINT32  PCE : 1;          ///< Performance Monitoring Counter Enable.
        UINT32  OSFXSR : 1;       ///< Operating System Support for FXSAVE and FXRSTOR instructions
        UINT32  OSXMMEXCPT : 1;   ///< Operating System Support for Unmasked SIMD Floating Point Exceptions.
        UINT32  UMIP : 1;         ///< User-Mode Instruction Prevention.
        UINT32  LA57 : 1;         ///< Linear Address 57bit.
        UINT32  VMXE : 1;         ///< VMX Enable.
        UINT32  SMXE : 1;         ///< SMX Enable.
        UINT32  Reserved_3 : 1;   ///< Reserved.
        UINT32  FSGSBASE : 1;     ///< FSGSBASE Enable.
        UINT32  PCIDE : 1;        ///< PCID Enable.
        UINT32  OSXSAVE : 1;      ///< XSAVE and Processor Extended States Enable.
        UINT32  Reserved_4 : 1;   ///< Reserved.
        UINT32  SMEP : 1;         ///< SMEP Enable.
        UINT32  SMAP : 1;         ///< SMAP Enable.
        UINT32  PKE : 1;          ///< Protection-Key Enable.
        UINT32  Reserved_5 : 9;   ///< Reserved.
    } Bits;
    UINT64     Uint64;
} CR4, * PCR4;

// Ritorna TRUE se per la CPU è possibile entrare in VMX Operation
BOOLEAN CpuIsVmxSupported();

// Imposta il bit 13 nel registro CR4 (VMX Enable)
// abilitando o meno la possibiltà di entrare in VMX Operation.
VOID CpuVmxEnable(BOOLEAN enable);

// Imposta bit di CR0 e CR4 a valori richiesti in VMX operation.
VOID CpuFixBits();