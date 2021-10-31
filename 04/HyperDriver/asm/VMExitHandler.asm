PUBLIC AsmVMExitHandler


EXTERN VmxVMExitHandler : PROC



.CODE _TEXT



AsmVMExitHandler PROC

	sub	rsp, 28h
	call	VmxVMExitHandler
	add	rsp, 28h	
	ret

AsmVMExitHandler ENDP

END