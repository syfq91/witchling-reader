#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session — WiFi/LWIP/netif
// teardown scatters long-lived allocations across the heap, leaving ~50KB of
// contiguous space unrecoverable without a reboot.

void silentRestart();                 // home screen
void silentRestartToReader();         // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToClockSettings();  // clock / time settings screen

// One-shot guarded variant for heap-fragmentation recovery: allows only one
// restart attempt across consecutive silent boots until a non-silent boot
// clears the latch.
bool trySilentRestartToReaderForHeapRecovery();
// Reboot immediately after an activity gives back exclusive raw storage (USB
// Drive). This is NOT a heap-defrag restart: the filesystem was unmounted and a
// USB host owned every sector, so every cache the firmware held — FAT state,
// covers, section caches, the open book's progress — is now potentially stale.
// A reboot is the only honest way back, and the RTC target lands setup() on
// Home rather than resuming a reader into a book that may no longer be there.
void restartToHomeAfterStorageHandoff();
