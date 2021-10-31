#pragma once

#pragma warning(disable : 4201)
typedef union
{
    struct
    {
        UINT32 DivideError : 1;
        UINT32 Debug : 1;
        UINT32 NmiInterrupt : 1;
        UINT32 Breakpoint : 1;
        UINT32 Overflow : 1;
        UINT32 Bound : 1;
        UINT32 InvalidOpcode : 1;
        UINT32 DeviceNotAvailable : 1;
        UINT32 DoubleFault : 1;
        UINT32 CoprocessorSegmentOverrun : 1;
        UINT32 InvalidTss : 1;
        UINT32 SegmentNotPresent : 1;
        UINT32 StackSegmentFault : 1;
        UINT32 GeneralProtection : 1;
        UINT32 PageFault : 1;
        UINT32 X87FloatingPointError : 1;
        UINT32 AlignmentCheck : 1;
        UINT32 MachineCheck : 1;
        UINT32 SimdFloatingPointError : 1;
        UINT32 VirtualizationException : 1;
    };
    UINT32 Uint32;
}EXCEPTION_BITMAP, *PEXCEPTION_BITMAP;
#pragma warning(default : 4201)