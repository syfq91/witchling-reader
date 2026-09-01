# Witch(hunt) Reader

This repository is a streamlined fork of [jpirnay/witchhunt-reader](https://github.com/jpirnay/witchhunt-reader) (originally derived from [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)).

For full documentation, hardware compatibility, feature comparisons, rendering samples, and installation instructions, please refer to the [upstream jpirnay/witchhunt-reader README](https://github.com/jpirnay/witchhunt-reader#readme).

---

## Changes Compared to `jpirnay/witchhunt-reader`

This fork focuses on a lighter memory footprint, improved reliability, and standard protocols by removing non-essential background features and legacy sync engines:

- **Removed Weather Integration**: Removed the Open-Meteo weather client, home screen weather widget, weather icon sets, settings, and background polling to conserve RAM and reduce battery drain.
- **Removed Calibre Wireless Device Transfer**: Dropped the proprietary Calibre SmartDevice wireless protocol in favor of standard OPDS library catalog browsing and browser-based file management.
- **Removed USB Serial File Transfer**: Stripped the custom USB-CDC serial transfer protocol and host scripts, keeping serial strictly for logging and diagnostics.
- **Removed Legacy KOReader Synchronization**: Removed the legacy KOReader sync client, XPath indexing/mapping engines, credential stores, and sync screens.
- **Decoupled Document ID & Reading Stats**: Replaced KOReader document hash generation with a lightweight, standalone `calculateBookId` helper in Reading Stats to preserve reading history, pace tracking, and cover caches.
