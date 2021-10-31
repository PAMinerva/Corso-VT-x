PUBLIC AsmRestoreToVMXOFFState
PUBLIC AsmSaveVMXOFFState

EXTERN g_StackPointerForReturning : QWORD
EXTERN g_BasePointerForReturning : QWORD

EXTERN __security_check_cookie : PROC


.code _text

; ------------------------------------------------------------------------

AsmRestoreToVMXOFFState PROC PUBLIC

; Esce dalla VMX operation
vmxoff

; Dopo le seguenti istruzioni RSP punta nuovamente ad indirizzo 
; di __vmx_vmlaunch in VmxInitialize e lo stack è nella stessa
; situazione di quando sono state salvati RSP ed RBP in AsmSaveVMXOFFState.
mov rsp, g_StackPointerForReturning
mov rbp, g_BasePointerForReturning

; NOTA: All'inizio di VmxInitialize c'è:

; push    rsi
; push    rdi
; sub     rsp, 78h

; per salvare (eventualmente) alcuni registri e fare spazio alle var locali: 
; potrebbe cambiare da compilazione a compilazione quindi meglio controllare con un disassembler.
; Ma prima di eseguire tali istruzione, su stack c'era Return Address a chiamante di 
; VmxInitialize, e cioè RSP puntava ad istruzione dopo call VmxInitialize in DriverCreate.
; Su stack c'è quindi lo spazio allocato all'inizio di VmxInitialize (con i vari push e sub)
; e poi l'indirizzo di __vmx_vmlaunch in VmxInitialize, quindi: 
; 0x78(push) + 8(push) + 8(push) + 8(RA vmlaunch) byte prima di arrivare a
; Return Address che punta all'istruzione successiva a call VmxInitialize in DriverCreate.

; Toglie gli 8 byte dell'RA a __vmx_vmlaunch in VmxInitialize dallo stack
ADD rsp, 8

; Simula il return TRUE
xor rax, rax
mov rax, 1

; Controlla con un qualsiasi disassembler come termina VmxInitialize.
; Dopo aver tolto anche questi 0x70 byte RSP punta ad istruzione successiva
; a call VmxInitialize in DriverCreate.
mov     rcx, [rsp + 88h -20h]
xor     rcx, rsp        ; StackCookie
call    __security_check_cookie
add     rsp, 78h
pop     rdi
pop     rsi
ret

AsmRestoreToVMXOFFState ENDP

; ------------------------------------------------------------------------

AsmSaveVMXOFFState PROC PUBLIC

; Salva RSP e RBP in variabili globali.
; Su stack in questo momento c'è Return Address (RA) a VmxInitialize: 
; RSP punta ad istruzione dopo call AsmSaveVMXOFFState in VmxInitialize,
; che in questo caso è __vmx_vmlaunch.

mov g_StackPointerForReturning, rsp
mov g_BasePointerForReturning, rbp
ret

AsmSaveVMXOFFState ENDP

; ------------------------------------------------------------------------

end