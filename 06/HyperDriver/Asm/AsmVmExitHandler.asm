PUBLIC AsmVmExitHandler

EXTERN VmxRootVmExitHandler : PROC
EXTERN VmxRootVmresume : PROC
EXTERN ReturnRSPForVmxoff : PROC
EXTERN ReturnRIPForVmxoff : PROC


.CODE _TEXT

;------------------------------------------------------------------------

AsmVmExitHandler PROC
    
    ; Crea uno spazio sullo stack dove verrà messo il return address
    ; all'istruzione successiva a VMCALL in AsmVmxNonRootVmcall, quando
    ; tale funzione è invocata per terminare la VMX operation. In altre
    ; parole è un modo per permettere al sistema operativo di continuare 
    ; ad eseguire il suo codice anche se non è più guest. A pensarci bene
    ; è una situazione molto simile a quella che si aveva all'inizio
    ; (quando si voleva continuare ad eseguire il codice dell'OS una volta
    ; che questo fosse diventato guest) ma in senso opposto.
    push 0 

    ; Salva lo stato del processore (al momento dell'esecuzione dell'istruzione 
    ; che ha portato a causare la VM exit) mettendo i valori dei registri
    ; sullo stack.

    ; RFLAGS
    pushfq

    ; RSP non serve sullo stack (dato che viene passato come parametro) a 
    ; VmxRootVmExitHandler ma per poter usare correttamente _GUEST_REGS è 
    ; necessario passare comunque un valore per tale registro. In questo
    ; caso si opta per ripetere push rbp una seconda volta.
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8        
    push rdi
    push rsi
    push rbp
    push rbp    ; rsp
    push rbx
    push rdx
    push rcx
    push rax

    ; Registri XMM
    sub     rsp ,80h
    movdqa  xmmword ptr [rsp], xmm0
    movdqa  xmmword ptr [rsp+10h], xmm1
    movdqa  xmmword ptr [rsp+20h], xmm2
    movdqa  xmmword ptr [rsp+30h], xmm3
    movdqa  xmmword ptr [rsp+40h], xmm4
    movdqa  xmmword ptr [rsp+50h], xmm5
    movdqa  xmmword ptr [rsp+60h], xmm6
    movdqa  xmmword ptr [rsp+70h], xmm7

    ; Passa il valore di RSP (che ora punta ad area di memoria con i registri salvati)
    ; al gestore delle VM exit come primo parametro (di tipo _GUEST_REGS). In questo 
    ; modo si permettere all'hypervisor di controllare l'esecuzione del codice guest.
	mov rcx, rsp

	sub	rsp, 28h                   ; Shadow space
	call	VmxRootVmExitHandler   ; invoca il gestore delle VM exit
	add	rsp, 28h                   ; Rimuove lo shadow space

    movdqa  xmm0, xmmword ptr [rsp]
    movdqa  xmm1, xmmword ptr [rsp+10h]
    movdqa  xmm2, xmmword ptr [rsp+20h]
    movdqa  xmm3, xmmword ptr [rsp+30h]
    movdqa  xmm4, xmmword ptr [rsp+40h]
    movdqa  xmm5, xmmword ptr [rsp+50h]
    movdqa  xmm6, xmmword ptr [rsp+60h]
    movdqa  xmm7, xmmword ptr [rsp+70h]
    add     rsp,  80h

	cmp	al, 1	; Controlla se si è eseguito VMXOFF per uscire da VMX operation (risultato in RAX)
	je		AsmVmxoffHandler

    ; ripristina stato del processore al momento dell'istruzione che ha portato a
    ; causare la VM exit
	RestoreState:
	pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp		; rsp
    pop rbp
    pop rsi
    pop rdi 
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    popfq

    ; Crea un po' di spazio sullo stack perché VmxRootVmresume è invocata
    ; con jump e non con call quindi siamo ancora nello stack frame della
    ; funzione che contiene l'istruzione che ha causato la VM exit.
    ; In questo caso VmxRootVmresume potrebbe, in teoria, sovrascrivere qualche
    ; valore che appartiene a tale stack frame e la cosa sarebbe da evitare.
	sub rsp, 0100h
	jmp VmxRootVmresume

AsmVmExitHandler ENDP

;------------------------------------------------------------------------

AsmVmxoffHandler PROC
    
    ; Shadow space 
    ; (20h e non 28h perché AsmVmxoffHandler non è invocato con call ma con jump quindi
    ; non viene inserito un indirizzo di ritorno sullo stack che lo disallinea)
    sub rsp, 020h

    ; Ritorna in RAX il valore di RSP al momento della VMCALL che ha causato la VM exit
    ; che ha portato ad eseguire VMXOFF: in questo caso quella in AsmVmxNonRootVmcall.
    call ReturnRSPForVmxoff

    add rsp, 020h       ; rimuove shadow space

    ; Salva il valore ritornato da ReturnRSPForVmxoff in spazio creato da push 0
    ; all'inizio di AsmVMExitHandler.
    ; In questo momento RSP punta a RAX pushato in AsmVMExitHandler. Ci sono 17 push
    ; da push rax a push 0: 
    ; 17 x 8 = 136 = 0x88 byte per arrivare ad indirizzo di 0 pushato sullo stack 
    ; all'inizio di AsmVMExitHandler.
    mov [rsp+088h], rax

    ; Mette in RAX il valore di RIP al momento della VMCALL che ha causato la VM exit
    ; che ha portato ad eseguire VMXOFF, aggiornato opportunamente per puntare
    ; all'istruzione successiva: pop r11 in AsmVmxNonRootVmcall.
    sub rsp, 020h
    call ReturnRIPForVmxoff
    add rsp, 020h

    ; Salva in RDX il valore di RSP corrente, che punta a valore di RAX pushato
    ; in AsmVmExitHandler
    mov rdx, rsp

    ; Salva in RBP il valore ritornato da ReturnRSPForVmxoff
    mov rbx, [rsp+088h]

    ; Ora RSP corrente punta a ciò che puntava al momento della VMCALL 
    ; che ha causato la VM exit che ha portato ad eseguire VMXOFF.
    mov rsp, rbx

    ; Mette l'indirizzo di pop r11 (che si trova in AsmVmxNonRootVmcall) su 
    ; stack usato dal guest al momento della VM exit.
    push rax

    ; Ripristina RSP, che punta nuovamente a valore di RAX pushato 
    ; in AsmVmExitHandler
    mov rsp, rdx
          
    ; Il valore ritornato da ReturnRSPForVmxoff è RSP al momento
    ; della VM exit che ha causato la VM exit che ha portato ad
    ; eseguire VMXOFF. Su tale stack, però, ora è stato aggiunto
    ; l'indirizzo di ritorno a pop r11. Quindi bisogna aggiornare
    ; lo stack pointer del guest per farlo puntare a tale valore.
    ; Ma RSP punta a RAX pushato in AsmVmExitHandler.
    ; E' questo il motivo per cui prima si è salvato lo stack pointer 
    ; in RBX, che punta ancora al valore ritornato da ReturnRSPForVmxoff 
    ; e che quindi può essere sottratto di 8.
    sub rbx, 08h

    ; Salva lo stack pointer aggiornato nello spazio creato da push 0
    ; all'inizio di AsmVMExitHandler.
    mov [rsp+088h], rbx

    ; Ripristina registri salvati all'inizio di AsmVMExitHandler
    ; (si ricordi che RSP punta ancora a valore di RAX pushato).
	RestoreState:

	pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp		         ; rsp
    pop rbp
    pop rsi
    pop rdi 
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    popfq

    ; Ora in cima allo stack c'è l'indirizzo di ritorno a
    ; pop r11 in AsmVmxNonRootVmcall. Mettendolo in RSP ed
    ; eseguento il ret si ritorna proprio a tale istruzione.
	pop		rsp
	ret

AsmVmxoffHandler ENDP

;------------------------------------------------------------------------

END