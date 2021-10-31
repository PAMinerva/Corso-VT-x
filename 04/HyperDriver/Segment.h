#pragma once

enum SEGREGS
{
	ES = 0,
	CS,
	SS,
	DS,
	FS,
	GS,
	LDTR,
	TR
};


typedef union {
    struct {
        UINT32  LimitLow : 16;    ///< Segment Limit 15..00
        UINT32  BaseLow : 16;     ///< Base Address  15..00
        UINT32  BaseMid : 8;      ///< Base Address  23..16
        UINT32  Type : 4;         ///< Segment Type (1 0 B 1)
        UINT32  S : 1;            ///< Descriptor Type
        UINT32  DPL : 2;          ///< Descriptor Privilege Level
        UINT32  P : 1;            ///< Segment Present
        UINT32  LimitHigh : 4;    ///< Segment Limit 19..16
        UINT32  AVL : 1;          ///< Available for use by system software
        UINT32  L   : 1;          ///< 0 0
        UINT32  Reserved_53 : 1;  ///< 0 0
        UINT32  G : 1;            ///< Granularity
        UINT32  BaseHigh : 8;     ///< Base Address  31..24
        UINT32  BaseUpper : 32;   ///< Base Address  63..32
        UINT32  Reserved_96 : 32; ///< Reserved
    } Bits;
    struct {
        UINT64  Uint64;
        UINT64  Uint64_1;
    } Uint128;
} SEGMENT_DESCRIPTOR, * PSEGMENT_DESCRIPTOR;


typedef union {
    struct {
        UINT16  Type : 4;
        UINT16  Sbit : 1;
        UINT16  Dpl : 2;
        UINT16  Present : 1;
        UINT16  Reserved_8 : 4;
        UINT16  Avl : 1;
        UINT16  Reserved_13 : 1;
        UINT16  Db : 1;
        UINT16  Granularity : 1;
        UINT16  Unusable : 1;
        UINT16  Reserved_17 : 15;
    } Bits;
    UINT32  Uint32;
} SEGMENT_ATTRIBUTES, *PSEGMENT_ATTRIBUTES;


#pragma pack(push, 1)
typedef struct
{
    UINT16 limit;
    UINT64 base_address;
} PSEUDO_DESCRIPTOR64, *PPSEUDO_DESCRIPTOR64;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct
{
    UINT16 limit;
    UINT32 base_address;
} PSEUDO_DESCRIPTOR32, *PPSEUDO_DESCRIPTOR32;
#pragma pack(pop)
