PUBLIC AsmVMExitHandler


EXTERN VmxVMExitHandler : PROC


.CODE _TEXT

AsmVMExitHandler PROC

	; shadow space/storage (4 * 8 byte + altri 8 byte per 
	; riallineare lo stack a 16 byte, disallineato a causa
	; dell'inserimento implicito del return address sullo stack
	; al momento della call). Totale 40 = 28h byte.
	sub	rsp, 28h
	call	VmxVMExitHandler
	add	rsp, 28h	
	ret

AsmVMExitHandler ENDP

END