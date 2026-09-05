# Witch(hunt) Reader

A streamlined fork of [jpirnay/witchhunt-reader](https://github.com/jpirnay/witchhunt-reader) (originally derived from [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)) exclusively targeting the **Xteink X3 and X4** (ESP32-C3 architecture).

For full device documentation, hardware specifications, and build guides, refer to the [upstream README](https://github.com/jpirnay/witchhunt-reader#readme).

---

## Changes in this Fork

This fork prioritizes physical button navigation, standard protocols, minimal memory overhead (~320KB RAM ceiling), and extended battery life:

- **Exclusively Supports Xteink X3 & X4 (ESP32-C3)**: Removed builds and dead code for touchscreen ESP32-S3 boards (X4 Pro, LilyGo T5S3). Unified on the ESP32-C3 single binary architecture.
- **Physical Button Navigation Only**: Completely removed touchscreen drivers, gesture recognition, touch tap zones, touch bands, and on-screen button hint bloat.
- **Removed USB Mass Storage (MSC) & Frontlight**: Removed TinyUSB MSC mode and frontlight controls to keep memory usage low and execution fast on ESP32-C3 hardware.
- **OPDS Progression 1.0 Sync**: Standardized reading progress synchronization ([OPDS Progression 1.0](https://github.com/opds-community/drafts/blob/main/opds-progression-1.0.md)) across EPUB, Markdown, TXT, and XTC readers with on-demand sync (via Reader Menu or remappable button controls) and opportunistic sync when WiFi is connected.
- **Removed Legacy Sync & Proprietary Protocols**: Dropped legacy KOReader sync and Calibre SmartDevice wireless transfer in favor of standard OPDS catalog downloads and OPDS Progression sync.
- **Removed Weather Integration**: Stripped Open-Meteo weather polling, home screen widgets, and weather icons to eliminate background network wakeups and save RAM.
- **Removed USB Serial File Transfer Protocol**: Removed custom binary serial transfer handling to keep serial output strictly dedicated to debugging.
- **Lightweight Reading Stats**: Standalone book ID generation without KOReader hashing dependencies, preserving reading history and pacing analytics.
