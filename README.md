# Witch(hunt) Reader

A streamlined fork of [jpirnay/witchhunt-reader](https://github.com/jpirnay/witchhunt-reader) (originally derived from [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)) for the Xteink X4 / ESP32-C3 e-reader.

For full device documentation, hardware specifications, and build guides, refer to the [upstream README](https://github.com/jpirnay/witchhunt-reader#readme).

---

## Changes in this Fork

This fork prioritizes standard protocols, minimal memory overhead (~380KB RAM ceiling), and extended battery life:

- **OPDS Progression 1.0 Sync**: Standardized reading progress synchronization ([OPDS Progression 1.0](https://github.com/opds-community/drafts/blob/main/opds-progression-1.0.md)) across EPUB, Markdown, TXT, and XTC readers with on-demand sync (via Reader Menu or remappable button controls) and opportunistic sync when WiFi is connected.
- **Removed Legacy Sync & Proprietary Protocols**: Dropped legacy KOReader sync and Calibre SmartDevice wireless transfer in favor of standard OPDS catalog downloads and OPDS Progression sync.
- **Removed Weather Integration**: Stripped Open-Meteo weather polling, home screen widgets, and weather icons to eliminate background network wakeups and save RAM.
- **Removed USB Serial File Transfer Protocol**: Removed custom binary serial transfer handling to keep serial output strictly dedicated to debugging.
- **Lightweight Reading Stats**: Standalone book ID generation without KOReader hashing dependencies, preserving reading history and pacing analytics.
