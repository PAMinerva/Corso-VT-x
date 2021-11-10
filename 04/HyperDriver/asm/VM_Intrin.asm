.CODE _TEXT                                                                                                                                                                          

; --------------------------------------------------

__read_ldtr PROC
    sldt ax
    ret
__read_ldtr ENDP

; --------------------------------------------------

__read_tr PROC
    str ax
    ret
__read_tr ENDP

; --------------------------------------------------

__read_cs PROC
    mov ax, cs
    ret
__read_cs ENDP

; --------------------------------------------------

__read_ss PROC
    mov ax, ss
    ret
__read_ss ENDP

; --------------------------------------------------

__read_ds PROC
    mov ax, ds
    ret
__read_ds ENDP

; --------------------------------------------------

__read_es PROC
    mov ax, es              
    ret
__read_es ENDP

; --------------------------------------------------

__read_fs PROC
    mov ax, fs
    ret
__read_fs ENDP

; --------------------------------------------------

__read_gs PROC
    mov ax, gs
    ret
__read_gs ENDP

; --------------------------------------------------

__sgdt PROC
    sgdt qword ptr [rcx]
    ret
__sgdt ENDP

; --------------------------------------------------

__sidt PROC
    sidt qword ptr [rcx]
    ret
__sidt ENDP

; --------------------------------------------------

__load_ar PROC
    lar rax, rcx
    jz no_error
    xor rax, rax
no_error:
    ret
__load_ar ENDP

; --------------------------------------------------

END