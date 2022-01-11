#include <ntddk.h>
#include <ntstrsafe.h>
#include "Logging.h"
#include "..\Support\Globals.h"
#include "Defs.h"


BOOLEAN LogInitialize() 
{
	// 2 LOG_BUFFER_INFORMATION per evitare dead-lock.
	// Uno per la root operation e l'altro per la non-root operation.
	MessageBufferInformation = ExAllocatePoolWithTag(NonPagedPool, sizeof(LOG_BUFFER_INFORMATION) * 2, POOLTAG);

	if (!MessageBufferInformation)
	{
		return FALSE;
	}

	RtlZeroMemory(MessageBufferInformation, sizeof(LOG_BUFFER_INFORMATION) * 2);

	// Initializza la variabile di sincronizzazione per lo spinlock custom 
	// da usare in root operation (HIGH_IRQL)
	VmxRootLoggingLock = 0;

	// Sia per la non-root (indice 0) che per la root operation (indice 1) ...
	for (int i = 0; i < 2; i++)
	{
		// Inizializza le variabili di sincronizzazione per gli spinlock di sistema.
		// In realtà tali variabili verranno usate esclusivamente in non-root
		// operation ma, per convenienza, si inizializzano anche quelle associate 
		// alla root operation (dove si opterà per gli spinlock custom, che usano 
		// variabili di sincronizzazione globali).
		KeInitializeSpinLock(&MessageBufferInformation[i].BufferLock);
		KeInitializeSpinLock(&MessageBufferInformation[i].BufferLockForNonImmMessage);

		// Alloca l'immediate buffer ed il non-immediate buffer.
		MessageBufferInformation[i].BufferStartAddress = (UINT64)ExAllocatePoolWithTag(NonPagedPool, IMM_BUFFER_SIZE, POOLTAG);
		MessageBufferInformation[i].BufferForMultipleNonImmediateMessage = (UINT64)ExAllocatePoolWithTag(NonPagedPool, MESSAGE_BODY_SIZE, POOLTAG);

		if (!MessageBufferInformation[i].BufferStartAddress)
		{
			return FALSE;
		}

		// Azzera l'immediate buffer.
		RtlZeroMemory((PVOID)MessageBufferInformation[i].BufferStartAddress, IMM_BUFFER_SIZE);

		// Imposta l'indirizzo della fine dell'immediate buffer.
		MessageBufferInformation[i].BufferEndAddress = (UINT64)MessageBufferInformation[i].BufferStartAddress + IMM_BUFFER_SIZE;
	}

	return TRUE;
}


VOID LogUnInitialize()
{
	// Libera lo spazio allocato in LogInitialize
	for (int i = 0; i < 2; i++)
	{
		ExFreePoolWithTag((PVOID)MessageBufferInformation[i].BufferStartAddress, POOLTAG);
		ExFreePoolWithTag((PVOID)MessageBufferInformation[i].BufferForMultipleNonImmediateMessage, POOLTAG);
	}

	ExFreePoolWithTag(MessageBufferInformation, POOLTAG);
}


NTSTATUS LogRegisterIrpBasedNotification(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	PNOTIFY_RECORD NotifyRecord;
	PIO_STACK_LOCATION IrpStack;

	//
	// Se GlobalNotifyRecord non è null vuol dire che c'è una richiesta (IRP) ancora
	// pendente del client che deve essere soddisfatta e quindi è inutile proseguire:
	// è necessario prima soddisfare quella pendente (quando ci saranno messaggi da leggere).
	// Per tale motivo, se GlobalNotifyRecord non è nullo, si ritorna semplicemente
	// STATUS_SUCCESS per indicare che la richiesta è stata soddisfatta (di fatto scartandola).
	//

	if (GlobalNotifyRecord == NULL)
	{
		IrpStack = IoGetCurrentIrpStackLocation(Irp);

		// Alloca spazio per le info della richiesta del Client (da liberare nella DPC)
		NotifyRecord = ExAllocatePoolWithQuotaTag(NonPagedPool, sizeof(NOTIFY_RECORD), POOLTAG);

		if (NULL == NotifyRecord) {
			return  STATUS_INSUFFICIENT_RESOURCES;
		}

		NotifyRecord->PendingIrp = Irp;

		// Inizializza il campo Dpc di NOTIFY_RECORD ed indica che la funzione
		// da invocare sarà LogNotifyUsermodeCallback.
		KeInitializeDpc(&NotifyRecord->Dpc, // Dpc
			LogNotifyUsermodeCallback,      // DeferredRoutine
			NotifyRecord                    // DeferredContext
		);

		// Marca l'IRP come pendente: da completare nella DPC.
		IoMarkIrpPending(Irp);

		//
		// Accoda DPC solo se c'è un messaggio già pronto da inviare al client
		// altrimenti salva la richiesta, che verrà soddisfatta quando il driver
		// avrà un messaggio da scrivere nell'immediate buffer.
		// 

		// Controlla se c'è un messaggio da inviare al client nell'immediate buffer.
		// Controlla prima quello della non-root operation perché si passa meno
		// tempo in tale modalità operativa e quindi è più probabile non trovare
		// messaggi in tale buffer.
		// Se si invertisse il controllo si rischierebbe di non leggere mai i
		// messaggi inviati dalla non-root operation perché l'immediate buffer della
		// root operation è quasi sempre pieno.
		if (LogCheckForNewMessage(FALSE))
		{
			// Imposta a FALSE la variabile sentinella che indica di prendere
			// il messaggio da inviare al client dall'immediate buffer della
			// non-root operation.
			NotifyRecord->CheckVmxRootMessageBuffer = FALSE;

			// Inserisce DPC nella coda della CPU corrente e la funzione
			// LogNotifyUsermodeCallback verrà invocata non appena l'IRQL
			// associato alla CPU sarà <= DISPATCH_LEVEL.
			KeInsertQueueDpc(&NotifyRecord->Dpc, NotifyRecord, NULL);
		}
		else if (LogCheckForNewMessage(TRUE))
		{
			// Imposta sentinella a TRUE e inserisce DPC nella coda della CPU.
			NotifyRecord->CheckVmxRootMessageBuffer = TRUE;
			KeInsertQueueDpc(&NotifyRecord->Dpc, NotifyRecord, NULL);
		}
		else
		{
			// Ancora nessun messaggio da leggere negli immediate buffer 
			// (quindi non si può accodare DPC) ma la richiesta del client 
			// viene registrata nella variabile globale definita per tale scopo.
			GlobalNotifyRecord = NotifyRecord;
		}

		// Si ritorna STATUS_PENDING per indicare che la richiesta non è ancora
		// stata completata.
		return STATUS_PENDING;
	}
	else
	{
		// Scarta la richiesta indicando che è stata soddisfatta.
		return STATUS_SUCCESS;
	}
}


// Funzione eseguita nel contesto di un processo random.
// Funziona perché Irp->AssociatedIrp.SystemBuffer punta a 
// memoria di sistema (visibile a tutti).
VOID LogNotifyUsermodeCallback(
	PKDPC Dpc, 
	PVOID DeferredContext, 
	PVOID SystemArgument1, 
	PVOID SystemArgument2)
{

	PNOTIFY_RECORD NotifyRecord;
	PIRP Irp;
	UINT32 Length;

	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);

	// DeferredContext è il PNOTIFY_RECORD passato a KeInsertQueueDpc
	// in LogRegisterIrpBasedNotification.
	NotifyRecord = DeferredContext;

	ASSERT(NotifyRecord != NULL);

	Irp = NotifyRecord->PendingIrp;

	if (Irp != NULL) 
	{
		PCHAR OutBuff;
		ULONG OutBuffLength;
		PIO_STACK_LOCATION IrpSp;

		IrpSp = IoGetCurrentIrpStackLocation(Irp);
		OutBuffLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

		if (!OutBuffLength)
		{
			Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);

			if (NotifyRecord != NULL)
				ExFreePoolWithTag(NotifyRecord, POOLTAG);

			return;
		}

		if (!Irp->AssociatedIrp.SystemBuffer)
		{
			Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);

			if (NotifyRecord != NULL)
				ExFreePoolWithTag(NotifyRecord, POOLTAG);

			return;
		}

		// Recupera il buffer di output user mode
		// (o meglio, la sua copia in kernel space)
		OutBuff = Irp->AssociatedIrp.SystemBuffer;
		Length = 0;

		// Copia il messaggio da leggere nel buffer di output user mode
		if (!LogReadBuffer(NotifyRecord->CheckVmxRootMessageBuffer, OutBuff, &Length))
		{
			// Se non c'era niente da leggere qualcosa è andato storto.
			Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);

			if (NotifyRecord != NULL)
				ExFreePoolWithTag(NotifyRecord, POOLTAG);

			return;
		}

		Irp->IoStatus.Information = Length;

		// Marca l'IRP come completato.
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}

	// Libera la memoria.
	if (NotifyRecord != NULL) {
		ExFreePoolWithTag(NotifyRecord, POOLTAG);
	}
}


BOOLEAN LogReadBuffer(BOOLEAN IsVmxRoot, PVOID BufferToSaveMessage, UINT32* ReturnedLength)
{
	KIRQL OldIRQL = 0;
	UINT32 Index;

	if (IsVmxRoot)
	{
		// Usa indice per root operation.
		Index = 1;

		// Acquisisce lock custom.
		SpinlockLock(&VmxRootLoggingLock);
	}
	else
	{
		// Usa indice per non-root operation.
		Index = 0;

		// Acquisisce lock con API di sistema.
		KeAcquireSpinLock(&MessageBufferInformation[Index].BufferLock, &OldIRQL);
	}

	// Recupera l'header del messaggio da inviare al client.
	BUFFER_HEADER* Header = (BUFFER_HEADER*)((UINT64)MessageBufferInformation[Index].BufferStartAddress + 
		(MessageBufferInformation[Index].CurrentIndexToSend * (MESSAGE_BODY_SIZE + sizeof(BUFFER_HEADER))));

	// Se non è valido non c'è niente da leggere.
	if (!Header->Valid)
		return FALSE;

	// Copia il codice operativo nel buffer di output.
	RtlCopyBytes(BufferToSaveMessage, &Header->OperationCode, sizeof(UINT32));

	// Recupera il corpo del messaggio da inviare al client.
	PVOID SendingBuffer = (PVOID)(MessageBufferInformation[Index].BufferStartAddress + 
		(MessageBufferInformation[Index].CurrentIndexToSend * 
			(MESSAGE_BODY_SIZE + sizeof(BUFFER_HEADER))) + sizeof(BUFFER_HEADER));

	// Aggiunge il corpo del messaggio nel buffer di output (dopo il codice operativo).
	PVOID SavingAddress = (PVOID)((UINT64)BufferToSaveMessage + sizeof(UINT32));
	RtlCopyBytes(SavingAddress, SendingBuffer, Header->BodySize);


	// Mostra i messaggi anche nel debugger
#if LOG_TO_DEBUGGER

	if (Header->OperationCode <= OPERATION_NON_IMMEDIATE_MESSAGE)
	{
		// Siamo in DISPATCH_LEVEL: si può usare DbgPrint.
		// DbgPrint riesce a trasmettere al massimo 512 byte di informazione.
		if (Header->BodySize > 512)
		{
			for (size_t i = 0; i <= Header->BodySize / 512; i++)
			{
				if (i != 0)
				{
					// In pratica, però, legge solo i primi 510 byte/caratteri ASCII.
					// Forse usa gli ultimi due per inserire il carattere nullo terminatore
					// (sinceramente non ho indagato).
					KdPrint(("%s", (char*)((UINT64)SendingBuffer + (512 * i) - 2)));
				}
				else
				{
					KdPrint(("%s", (char*)((UINT64)SendingBuffer)));
				}
			}
		}
		else
		{
			KdPrint(("%s", (char*)SendingBuffer));
		}

	}

#endif


	// Una volta scritto nel buffer di output, il messaggio nell'immediate buffer
	// può essere marcato come invalido/letto.
	Header->Valid = FALSE;

	// Ritorna al client il numero di byte scritti nel buffer di output.
	*ReturnedLength = Header->BodySize + sizeof(UINT32);

	// Azzera il corpo del messaggio.
	RtlZeroMemory(SendingBuffer, Header->BodySize);

	if (MessageBufferInformation[Index].CurrentIndexToSend > MAX_MESSAGE_NUMBER - 1)
	{
		// Se l'ultimo messaggio letto era l'elemento finale dell'immediate buffer
		// si riparte dall'inizio.
		MessageBufferInformation[Index].CurrentIndexToSend = 0;
	}
	else
	{
		// Altrimenti incrementa l'indice del messaggio da inviare al client.
		MessageBufferInformation[Index].CurrentIndexToSend = MessageBufferInformation[Index].CurrentIndexToSend + 1;
	}

	// Rilascia il lock.
	if (IsVmxRoot)
	{
		SpinlockUnlock(&VmxRootLoggingLock);
	}
	else
	{
		KeReleaseSpinLock(&MessageBufferInformation[Index].BufferLock, OldIRQL);
	}

	return TRUE;
}


BOOLEAN LogSendBuffer(UINT32 OperationCode, PVOID Buffer, UINT32 BufferLength)
{
	KIRQL OldIRQL = 0;
	UINT32 Index;
	BOOLEAN IsVmxRoot;

	if (BufferLength > MESSAGE_BODY_SIZE - 1 || BufferLength == 0)
	{
		return FALSE;
	}

	IsVmxRoot = GuestState[KeGetCurrentProcessorNumber()].IsOnVmxRootMode;

	if (IsVmxRoot)
	{
		Index = 1;
		SpinlockLock(&VmxRootLoggingLock);
	}
	else
	{
		Index = 0;
		KeAcquireSpinLock(&MessageBufferInformation[Index].BufferLock, &OldIRQL);
	}

	// Se l'ultimo messaggio scritto era l'elemento finale dell'immediate buffer
	// si riparte dall'inizio.
	if (MessageBufferInformation[Index].CurrentIndexToWrite > MAX_MESSAGE_NUMBER - 1)
	{
		MessageBufferInformation[Index].CurrentIndexToWrite = 0;
	}

	// Recupera l'header del messaggio da scrivere nell'immediate buffer.
	BUFFER_HEADER* Header = (BUFFER_HEADER*)(MessageBufferInformation[Index].BufferStartAddress + 
		(MessageBufferInformation[Index].CurrentIndexToWrite * (MESSAGE_BODY_SIZE + sizeof(BUFFER_HEADER))));

	// Imposta l'header
	Header->OperationCode = OperationCode;
	Header->BodySize = BufferLength;
	Header->Valid = TRUE;

	// Recupera il corpo del messaggio da scrivere nell'immediate buffer.
	PVOID SavingBuffer = (PVOID)(MessageBufferInformation[Index].BufferStartAddress + 
		(MessageBufferInformation[Index].CurrentIndexToWrite * (MESSAGE_BODY_SIZE + sizeof(BUFFER_HEADER))) + sizeof(BUFFER_HEADER));

	// Copia il messaggio nell'elemento dell'immediate buffer in cui scrivere.
	RtlCopyBytes(SavingBuffer, Buffer, BufferLength);

	// Incrementa l'indice dell'elemento in cui scrivere.
	MessageBufferInformation[Index].CurrentIndexToWrite = MessageBufferInformation[Index].CurrentIndexToWrite + 1;

	// Se c'era una richiesta del client ancora pendente si può soddisfare qui dato che
	// ora un messaggio da fargli leggere c'è.
	if (GlobalNotifyRecord != NULL)
	{
		GlobalNotifyRecord->CheckVmxRootMessageBuffer = IsVmxRoot;

		// Accoda la DPC (che soddisferà la richiesta) alla CPU corrente.
		KeInsertQueueDpc(&GlobalNotifyRecord->Dpc, GlobalNotifyRecord, NULL);

		// La richiesta verrà soddisfatta ed il relativo IRP completato
		// così si può impostare a null la variabile globale che conservava
		// la richiesta pendente.
		GlobalNotifyRecord = NULL;
	}

	if (IsVmxRoot)
	{
		SpinlockUnlock(&VmxRootLoggingLock);
	}
	else
	{
		KeReleaseSpinLock(&MessageBufferInformation[Index].BufferLock, OldIRQL);
	}

	return TRUE;
}


BOOLEAN LogSendMessageToQueue(
	UINT32 OperationCode, 
	BOOLEAN IsImmediateMessage, 
	BOOLEAN ShowCurrentSystemTime, 
	LPCCH Fmt, ...)
{
	BOOLEAN Result;
	va_list ArgList;
	size_t WrittenSize;
	UINT32 Index;
	KIRQL OldIRQL = 0;
	BOOLEAN IsVmxRootMode;
	int SprintfResult;
	char LogMessage[MESSAGE_BODY_SIZE];
	char TempMessage[MESSAGE_BODY_SIZE];
	char TimeBuffer[20] = { 0 };

	IsVmxRootMode = GuestState[KeGetCurrentProcessorNumber()].IsOnVmxRootMode;

	if (ShowCurrentSystemTime)
	{
		va_start(ArgList, Fmt);

		// Copia la stringa di formato in TempMessage.
		// Non si possono usare metodi RtlXXX perché questi richiedono IRQL <= PASSIVE_LEVEL
		// il che non è garantito in root operation (in cui non c'è un vero e proprio IRQL
		// ma in pratica è come se fosse HIGH_IRQL).
		// MESSAGE_BODY_SIZE - 1 perché il client si aspetta un carattere nullo terminatore
		// (si ricordi che i corpi dei messaggi vengono azzerati una volta inviati al client).
		SprintfResult = vsprintf_s(TempMessage, MESSAGE_BODY_SIZE - 1, Fmt, ArgList);

		va_end(ArgList);

		if (SprintfResult == -1)
			return FALSE;

		// Calcola tempistica del messaggio.
		TIME_FIELDS TimeFields;
		LARGE_INTEGER SystemTime, LocalTime;
		KeQuerySystemTime(&SystemTime);
		ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
		RtlTimeToTimeFields(&LocalTime, &TimeFields);

		sprintf_s(TimeBuffer, RTL_NUMBER_OF(TimeBuffer), "%02hd:%02hd:%02hd.%03hd", TimeFields.Hour,
			TimeFields.Minute, TimeFields.Second,
			TimeFields.Milliseconds);

		// Compone il messaggio e lo mette in LogMessage.
		SprintfResult = sprintf_s(LogMessage, MESSAGE_BODY_SIZE - 1, "(%s - core : %d - vmx-root? %s)   %s", 
			TimeBuffer, KeGetCurrentProcessorNumberEx(0), IsVmxRootMode ? "yes" : "no", TempMessage);

		if (SprintfResult == -1)
			return FALSE;
	}
	else
	{
		// Niente tempistica quindi compone il messaggio semplicemente copiandolo dopo aver
		// usato vsprintf_s per passare gli argomenti ai parametri nella stringa di formato.
		va_start(ArgList, Fmt);
		SprintfResult = vsprintf_s(LogMessage, MESSAGE_BODY_SIZE - 1, Fmt, ArgList);
		va_end(ArgList);

		if (SprintfResult == -1)
			return FALSE;

	}

	// Ritorna byte\caratteri del messaggio.
	WrittenSize = strnlen_s(LogMessage, MESSAGE_BODY_SIZE - 1);

	if (LogMessage[0] == '\0')
		return FALSE;

	if (IsImmediateMessage)
	{
		// Scrive il messaggio nell'immediate buffer.
		return LogSendBuffer(OperationCode, LogMessage, (UINT32)WrittenSize);
	}
	else
	{
		//
		// Scrive il messaggio nel non-immediate buffer.
		//

		// L'accesso al non-immediate buffer è sincronizzato tramite spin lock.
		if (IsVmxRootMode)
		{
			Index = 1;
			SpinlockLock(&VmxRootLoggingLockForNonImmBuffers);
		}
		else
		{
			Index = 0;
			KeAcquireSpinLock(&MessageBufferInformation[Index].BufferLockForNonImmMessage, &OldIRQL);
		}

		Result = TRUE;

		// Se con i WrittenSize byte del messaggio corrente si supera la dimensione del
		// non-immediate buffer allora bisogna scriverlo nell'immediate buffer, pronto
		// per essere inviato.
		if ((MessageBufferInformation[Index].CurrentLengthOfNonImmBuffer + WrittenSize) > MESSAGE_BODY_SIZE - 1 && 
			MessageBufferInformation[Index].CurrentLengthOfNonImmBuffer != 0)
		{

			// Copia il messaggio dal buffer non-immediate a quello immediate.
			Result = LogSendBuffer(OPERATION_NON_IMMEDIATE_MESSAGE,
				(PVOID)MessageBufferInformation[Index].BufferForMultipleNonImmediateMessage,
				MessageBufferInformation[Index].CurrentLengthOfNonImmBuffer);

			// Azzera il non-immediate buffer al fine di riutilizzarlo una volta 
			// usciti dal blocco IF corrente.
			MessageBufferInformation[Index].CurrentLengthOfNonImmBuffer = 0;
			RtlZeroMemory((PVOID)MessageBufferInformation[Index].BufferForMultipleNonImmediateMessage, MESSAGE_BODY_SIZE);
		}

		// Copia il messaggio nel non-immediate buffer accodandolo ai precedenti.
		RtlCopyBytes((PVOID)(MessageBufferInformation[Index].BufferForMultipleNonImmediateMessage +
			MessageBufferInformation[Index].CurrentLengthOfNonImmBuffer), LogMessage, WrittenSize);

		// Aggiunge la dimensione del messaggio a quella del non-immediate buffer.
		MessageBufferInformation[Index].CurrentLengthOfNonImmBuffer += (UINT32)WrittenSize;

		// Rilascia lock per consentire ad altri di scrivere sul non-immediate buffer.
		if (IsVmxRootMode)
		{
			SpinlockUnlock(&VmxRootLoggingLockForNonImmBuffers);
		}
		else
		{
			KeReleaseSpinLock(&MessageBufferInformation[Index].BufferLockForNonImmMessage, OldIRQL);
		}

		return Result;
	}
}


BOOLEAN LogCheckForNewMessage(BOOLEAN IsVmxRoot) 
{
	UINT32 Index;

	// 0 indice per non-root operation
	// 1 indice per root operation
	if (IsVmxRoot)
	{
		Index = 1;
	}
	else
	{
		Index = 0;
	}

	// Recupera l'header del messaggio da inviare al client ...
	BUFFER_HEADER* Header = (BUFFER_HEADER*)((UINT64)MessageBufferInformation[Index].BufferStartAddress + 
		(MessageBufferInformation[Index].CurrentIndexToSend * (MESSAGE_BODY_SIZE + sizeof(BUFFER_HEADER))));

	// ... se non è valido non c'è niente da inviare.
	if (!Header->Valid)
		return FALSE;

	// Restituisce TRUE se c'è un messaggio da inviare.
	return TRUE;
}


inline BOOLEAN SpinlockTryLock(volatile LONG* Lock)
{
	// Ritorna TRUE se la variabile di sincronizzazione Lock
	// non è già impostata e se è possibile impostarla ad 1
	// in un operazione atomica (che non può essere interrotta).
	// In tal caso si puà considerare acquisito il lock.
	return (!(*Lock) && !_interlockedbittestandset(Lock, 0));
}


void SpinlockLock(volatile LONG* Lock)
{
	unsigned wait = 1;

	while (!SpinlockTryLock(Lock))
	{
		for (unsigned i = 0; i < wait; ++i)
		{
			/* PAUSE  —  Spin Loop Hint
			Improves the performance of spin-wait loops. When executing a “spin-wait loop”,
			processors will suffer a severe performance penalty when exiting the loop because 
			it detects a possible memory order violation. The PAUSE instruction provides a hint 
			to the processor that the code sequence is a spin-wait loop. The processor uses this 
			hint to avoid the memory order violation in most situations, which greatly improves 
			processor performance. For this reason, it is recommended that a PAUSE instruction 
			be placed in all spin-wait loops */
			_mm_pause();
		}

		// Invocare PAUSE troppe volte impatta negativamente sulle performance quindi
		// se wait diventa troppo grande si cappa il suo valore.

		if (wait * 2 > 65536)
		{
			wait = 65536;
		}
		else
		{
			wait = wait * 2;
		}
	}
}


void SpinlockUnlock(volatile LONG* Lock)
{
	*Lock = 0;
}
