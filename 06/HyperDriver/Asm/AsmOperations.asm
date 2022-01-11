PUBLIC AsmRestoreState
PUBLIC AsmSaveState
PUBLIC AsmVmxNonRootVmcall
PUBLIC AsmHypervVmcall

EXTERN VirtualizeCurrentCpu : PROC


.CODE _TEXT

; ------------------------------------------------------------------------

AsmSaveState PROC PUBLIC

	; Sullo stack ora c'è l'indirizzo di ritorno dell'istruzione successiva a 
	; quella dell'invocazione di AsmSaveState in CpuBroadcastRoutine. In altre
	; parole, RSP punta a KeLowerIrql(OldIrql) in CpuBroadcastRoutine.

	; Salva lo stato del processore

	pushfq	; salva RFLAGS

	push rax
	push rcx
	push rdx
	push rbx
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	; shadow space
	sub rsp, 28h

	; Invoca VirtualizeCurrentCpu.
	; AsmSaveState e VirtualizeCurrentCpu hanno lo stesso primo parametro.
	; Entrambi i metodi usano la convenzione di chiamata __fastcall dunque
	; il primo parametro si trova in RCX. Dato che AsmSaveState non tocca
	; mai tale registro significa che VirtualizeCurrentCpu riceverà
	; lo stesso parametro senza che si debba fare nulla.
	; Ma VirtualizeCurrentCpu prende anche un secondo parametro, il valore
	; di RSP con cui inizializzare il relativo campo della VMCS e che verrà
	; usato come stack pointer dal guest, in non-root operation, una volta lanciato. 
	; Secondo la convenzione di chiamata __fastcall il secondo parametro va in RDX.
	; Perché mai il guest dovrebbe avere uno stack dove ci sono i registri salvati
	; al momento dell'invocazione di AsmSaveState? L'idea è quella di inizializzare
	; la VMCS e lanciare il guest facendogli eseguire la funzione AsmRestoreState
	; (vedi sotto) che ripristina\carica lo stato del processore al momento 
	; dell'invocazione di AsmSaveState ed al ret ritorna all'istruzione successiva
	; all'invocazione di AsmSaveState in CpuBroadcastRoutine, in questo caso 
	; KeLowerIrql(OldIrql);
	; L'effetto sarà quello di virtualizzare la CPU proseguendo con l'esecuzione del
	; codice che si stava eseguendo prima di entrare in VMX operation e lanciare il 
	; guest, rendendo di fatto l'intero OS (Windows) il guest ed il nostro driver 
	; l'hypervisor.

	mov rdx, rsp
	call VirtualizeCurrentCpu

	; non si arriva mai qui
	ret

AsmSaveState ENDP

; ------------------------------------------------------------------------

AsmRestoreState PROC PUBLIC

	; elimina lo shadow space
	add rsp, 28h

	; ripristina registri, e quindi lo stato del processore

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdi
	pop rsi
	pop rbp
	pop rbx
	pop rdx
	pop rcx
	pop rax

	popfq	; ripristina RFLAGS
	
	ret     ; ritorna a KeLowerIrql(OldIrql) in CpuBroadcastRoutine

AsmRestoreState ENDP

; ------------------------------------------------------------------------

AsmVmxNonRootVmcall PROC
    
    ; Imposta i valori esadecimali delle stringhe ASCII "NOHYPERV" e "VMCALL" nei 
	; registri R10 ed R11 (che non sono coinvolti nella convenzione di chiamata 
	; __fastcall) così si è sicuri che la VMCALL è indirizzata al nostro hypervisor 
	; (e non a Hyper-V).
    ; Segnala al nostro hypervisor di gestire la relativa VM exit.
    pushfq
    push    r10
    push    r11
    mov     r10, 4e4f485950455256H   ; [NOHYPERV]
    mov     r11, 564d43414c4cH       ; [VMCALL]

	; Causa VM exit che porterà ad invocazione di 
	; VmxRootVmcallHandler(rcx = vmcallnumber, rdx = optparam1, r8 = optparam2, r9 = optparam3)
	; all'interno del gestore delle VM exit 
    vmcall                           
    pop     r11
    pop     r10
    popfq
    ret                              ; Ritorna NTSTATUS che si trova in RAX (valore di ritorno di VmxRootVmcallHandler)

AsmVmxNonRootVmcall ENDP


;------------------------------------------------------------------------

AsmHypervVmcall PROC

	; Esegue VMCALL in VMX non-root operation (a seguito di una VMCALL destinata
	; ad Hyper-V ma che Hyper-V non ha gestito in quanto viene passata prima
	; al nostro driver per fargli credere di essere l'hypervisor reale in
	; root operation). 
	; Questo causa una nuova VM exit che indica ad Hyper-V che non eravamo
	; interessati alla gestione della VM exit originaria e rimandiamo ad esso 
	; ogni responsabilità.
	; Una volta che Hyper-V ha gestito la VMCALL si ritorna qui, ad
	; istruzione successiva a VMCALL: in questo caso RET.
	; Le VMCALL indirizzate a Hyper-V si chiamano HyperCall e ritornano 0x0000 
	; se completate con successo.
    vmcall
    ret       ; Ritorna HV_STATUS che si trova in RAX (valore di ritorno della HyperCall)

AsmHypervVmcall ENDP

;------------------------------------------------------------------------

END