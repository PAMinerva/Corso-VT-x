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

; UINT32 __load_ar(UINT16 selector (rcx));

; LAR imposta a 0 il flag ZF in RFLAGS se non è in grado di
; leggere gli attributi del segment descriptor a cui si riferisce
; il segment selector passato nel secondo operando (rcx in questo
; caso) e non inserisce nulla nel primo operando (rax in questo caso).
; Per tale motivo, se LAR fallisce si imposta RAX a 0 (xorando)
; e si ritorna tale risultato al chiamante di __load_ar.

__load_ar PROC
    lar rax, rcx
    jz no_error
    xor rax, rax
no_error:
    ret
__load_ar ENDP

; --------------------------------------------------

; __reload_gdtr(PVOID GdtBase (rcx), ULONG GdtLimit (rdx));

; Si ricordi che GDTR è un "registro" di 80 bit composto da due campi contigui: 
; Limit:Base
; Limit è un campo a 16 bit che indica la dimensione in byte della GDT 
; (o, se si preferisce, l'offset in byte a partire dal campo Base).
; Base è un campo a 64 bit che indica l'inizio della GDT.

__reload_gdtr PROC
	push rcx                   ; mette GdtBase sullo stack
	shl rdx, 48                ; sposta GdtLimit nei 16 bit alti di RDX
	push rdx                   ; mette GdtLimit sullo stack
	lgdt fword ptr [rsp+6]     ; RSP+6 salta i primi 48 bit e punta a GdtLimit:GdtBase
	pop rax                    ; dismette i due valori pushati mettendoli in RAX...
	pop rax                    ; ...tanto valore di ritorno non viene usato.
	ret
__reload_gdtr ENDP

; --------------------------------------------------

; __reload_idtr(PVOID IdtBase (rcx), ULONG IdtLimit (rdx));

; Vale lo stesso discorso fatto per __reload_gdtr

__reload_idtr PROC
	push rcx
	shl	 rdx, 48
	push rdx
	lidt fword ptr [rsp+6]
	pop	rax
	pop	rax
	ret
__reload_idtr ENDP

; --------------------------------------------------

END