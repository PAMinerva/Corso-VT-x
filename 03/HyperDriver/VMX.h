#pragma once

VOID VmxInitializeDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
VOID VmxInitializeAffinity();