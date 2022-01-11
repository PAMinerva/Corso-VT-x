#pragma once

#define MAX_MESSAGE_NUMBER      1000 // numero di messaggi
#define MESSAGE_BODY_SIZE		1000 // dimensione dei messaggi in byte

// Buffer user mode è composto dal codice operativo, dal corpo del messaggio e dal carattere terminatore nullo.
#define USER_MODE_BUFFER_SIZE  sizeof(UINT32) + MESSAGE_BODY_SIZE + 1 

// Immediate Buffer in kernel mode è composto da un certo numero di messaggi composti da header e corpo.
#define IMM_BUFFER_SIZE MAX_MESSAGE_NUMBER * (MESSAGE_BODY_SIZE + sizeof(BUFFER_HEADER))

// IO Control code per registrare le richieste del client di leggere un messaggio
#define IOCTL_REGISTER_IRP \
   CTL_CODE( FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS )

// IO Control code per indicare al driver che il client non invierà più richieste
#define IOCTL_RETURN_IRP_PENDING_PACKETS_AND_DISALLOW_IOCTL \
   CTL_CODE( FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS )


//////////////////////////////////////////////////
//				Codici operativi				//
//////////////////////////////////////////////////

#define OPERATION_INFO_MESSAGE							0x1
#define OPERATION_WARNING_MESSAGE						0x2
#define OPERATION_ERROR_MESSAGE							0x3
#define OPERATION_NON_IMMEDIATE_MESSAGE					0x4
