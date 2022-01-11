#pragma once

#include "Defs.h"

//////////////////////////////////////////////////
//					Strutture					//
//////////////////////////////////////////////////

// Info per soddisfare la richiesta del Client
typedef struct _NOTIFY_RECORD {
	PIRP            PendingIrp;
	KDPC            Dpc;
	BOOLEAN			CheckVmxRootMessageBuffer;
} NOTIFY_RECORD, * PNOTIFY_RECORD;


// Header del messaggio
typedef struct _BUFFER_HEADER {
	UINT32 OperationCode;	// Codice operativo che identifica il tipo di messaggio
	UINT32 BodySize;	    // Dimensione in byte del corpo del messaggio
	BOOLEAN Valid;			// Indica se il messaggio deve essere ancora inviato
} BUFFER_HEADER, * PBUFFER_HEADER;

// Info per gestire i buffer (immediate e non) in entrambi i contesti (root e non-root operation)
typedef struct _LOG_BUFFER_INFORMATION {

	UINT64 BufferStartAddress;		// Indirizzo di partenza dell'immediate buffer
	UINT64 BufferEndAddress;		// Indirizzo dove finisce l'immediate buffer

	UINT64 BufferForMultipleNonImmediateMessage;	// Indirizzo di partenza del non-immediate buffer
	UINT32 CurrentLengthOfNonImmBuffer;				// Dimensione in byte del non-immediate buffer

	KSPIN_LOCK BufferLock;						// Lock che protegge l'immediate buffer in non-root
	KSPIN_LOCK BufferLockForNonImmMessage;		// Lock che protegge il non-immediate buffer in non-root

	UINT32 CurrentIndexToSend;		// Indice (all'interno dell'immediate buffer) del messaggio da inviare al Client
	UINT32 CurrentIndexToWrite;		// Indice (all'interno dell'immediate buffer) in cui scrivere il messaggio

} LOG_BUFFER_INFORMATION, * PLOG_BUFFER_INFORMATION;



//////////////////////////////////////////////////
//				Variabili globali   			//
//////////////////////////////////////////////////

// Puntatore ad un array di 2 LOG_BUFFER_INFORMATION
// (uno per la root operation e l'altro per la non-root operation)
PLOG_BUFFER_INFORMATION MessageBufferInformation;

// Variabile di sincronizzazione per lo spinlock custom da usare
// con l'immediate buffer in root operation.
volatile LONG VmxRootLoggingLock;

// Variabile di sincronizzazione per lo spinlock custom da usare
// con il non-immediate buffer in root operation
volatile LONG VmxRootLoggingLockForNonImmBuffers;

// Indica se il Client può inviare richieste al driver tramite IOCTL
BOOLEAN AllowIOCTLFromUsermode;



//////////////////////////////////////////////////
//					Metodi  					//
//////////////////////////////////////////////////

// Alloca la memoria ed inizializza i dati necessari alla comunicazione.
BOOLEAN LogInitialize();

// Rilascia la memoria allocata durante LogInitialize.
VOID LogUnInitialize();

// Aggiunge un messaggio all'immediate buffer.
// Ritorna TRUE se l'operazione è avvenuta con successo, FALSE altrimenti.
BOOLEAN LogSendBuffer(UINT32 OperationCode, PVOID Buffer, UINT32 BufferLength);

// Copia il messaggio da inviare al client nel buffer di output user mode.
// Ritorna TRUE se l'operazione è avvenuta con successo.
// Ritorna FALSE se non c'è nessun messaggio da inviare.
BOOLEAN LogReadBuffer(BOOLEAN IsVmxRoot, PVOID BufferToSaveMessage, UINT32* ReturnedLength);

// Controlla se ci sono messaggi da inciare al client.
BOOLEAN LogCheckForNewMessage(BOOLEAN IsVmxRoot);

// Compone il messaggio e lo copia nel non-immediate buffer oppure
// nell'immediate buffer (tramite LogSendBuffer).
BOOLEAN LogSendMessageToQueue(UINT32 OperationCode, BOOLEAN IsImmediateMessage, BOOLEAN ShowCurrentSystemTime, LPCCH Fmt, ...);

// DPC che soddisfa la richiesta del client.
VOID LogNotifyUsermodeCallback(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

// Registra e cerca di soddisfare (oppure salva) una richiesta del client.
NTSTATUS LogRegisterIrpBasedNotification(PDEVICE_OBJECT DeviceObject, PIRP Irp);



//////////////////////////////////////////////////
//				 Spinlock Custom				//
//////////////////////////////////////////////////

// Sfrutta il supporto all'atomicità di alcune operazioni di
// base della CPU per impostare la variabile di sincronizzazione
// usata per implementare lo spinlock custom.
inline BOOLEAN SpinlockTryLock(volatile LONG* Lock);

// Acquisisce il lock.
inline void SpinlockLock(volatile LONG* Lock);

// Rilascia il lock.
inline void SpinlockUnlock(volatile LONG* Lock);



//////////////////////////////////////////////////
//					Macro   					//
//////////////////////////////////////////////////

#define LogInfo(format, ...)  \
    LogSendMessageToQueue(OPERATION_INFO_MESSAGE, FALSE, TRUE, "[+] Information (%s:%d) | " format "\n",	\
		 __func__, __LINE__, __VA_ARGS__)

#define LogInfoImmediate(format, ...)  \
    LogSendMessageToQueue(OPERATION_INFO_MESSAGE, TRUE, TRUE, "[+] Information (%s:%d) | " format "\n",	\
		 __func__, __LINE__, __VA_ARGS__)

#define LogWarning(format, ...)  \
    LogSendMessageToQueue(OPERATION_WARNING_MESSAGE, FALSE, TRUE, "[-] Warning (%s:%d) | " format "\n",	\
		__func__, __LINE__, __VA_ARGS__)

#define LogError(format, ...)  \
    LogSendMessageToQueue(OPERATION_ERROR_MESSAGE, FALSE, TRUE, "[!] Error (%s:%d) | " format "\n",	\
		 __func__, __LINE__, __VA_ARGS__);	\
		DbgBreakPoint()

#define Log(format, ...)  \
    LogSendMessageToQueue(OPERATION_INFO_MESSAGE, FALSE, TRUE, format "\n", __VA_ARGS__)


#define LOG_TO_DEBUGGER TRUE