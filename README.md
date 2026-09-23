# Witchling Reader

A lightweight, streamlined fork of [jpirnay/witchhunt-reader](https://github.com/jpirnay/witchhunt-reader) tailored exclusively for the locked **Xteink X4** (physical buttons, no touch, no PSRAM).

For full original documentation, refer to the [upstream README](https://github.com/jpirnay/witchhunt-reader#readme).

---

## Changes in this Fork

### Added
- **OPDS Progression 1.0**: Automatically syncs reading progress with compatible OPDS servers (e.g., Kavita, Audiobookshelf).
- **Simplified Status Bar**: Streamlined 5-option status bar customization (Top/Bottom, configurable Left, Middle, and Right slots for battery, page count, percentage, book title, and chapter title, plus a progress bar).
- **Larger Reading Sizes**: Added 20pt, 22pt, 24pt, and 26pt text sizes with sharper headings and smooth font scaling.
- **Menu Text Size**: Selectable **UI Font Size** (Normal / Large) in Settings for easier menu readability.
- **File & Folder Management**: Create folders, move books/folders, and delete folders directly within the file browser.
- **Visual Busy Indicator**: Subtle hourglass indicator during slower screen transitions so you know the device is working.
- **Quick Resume**: Press the Back button on the home screen to instantly jump back into your current book.

### Removed
- **TXT and Markdown Reader**: Pruned to focus exclusively on an optimized, fast EPUB experience.
- **Unused Subsystems**: Removed Clock / Timezones, Weather integration, and Reading Statistics to save memory and battery.
- **KOReader Sync & Calibre Wireless Transfer**: Simplified the networking stack in favor of direct OPDS sync.
- **Extra Themes & Touch Code**: Removed touch layer overhead for the X4 hardware; kept Lyra as the sole theme with borderless button hints.
- **Non-English Translations**: Pruned non-English translations to keep firmware size lean and fast.

### Fixed
- **EPUB Image Metadata**: Fixed memory exhaustion and crashes on EPUBs with large JPEG metadata headers (upstream #249).
- **Faster SD Card Access**: Batched data transfers from the SD card for snappier page turns and library browsing.
- **Font & Size Switching**: Fixed issues where changing fonts or sizes in the reader menu did not take effect until closing and reopening the book.
- **Button Responsiveness**: Fixed missed button presses during screen redraws in long lists.

---

## Building

Requires [PlatformIO Core](https://platformio.org/install/cli) (`pio`) — or VS Code with the PlatformIO IDE extension.

```sh
# Clone with submodules (or run `git submodule update --init --recursive` after cloning without)
git clone https://github.com/syfq91/witchling-reader.git
cd witchling-reader

# Enable repo-managed git hooks (once per clone)
git config core.hooksPath .githooks && chmod +x .githooks/pre-commit

# Build firmware binary (.pio/build/default/firmware.bin)
pio run

# Build release binary
pio run -e gh_release
```

## Installing Firmware

> [!NOTE]
> Locked X4 hardware does not support flashing over standard USB data.

To install or update firmware on the device:

- **SD Card Update**: Copy the compiled binary (`.pio/build/default/firmware.bin`) to the SD card root as `update.bin`, or navigate to any `.bin` file in the device file browser, long-press **Confirm**, and select **Flash**. Alternatively, navigate to **Settings -> System -> SD Firmware Update**.
- **Wi-Fi OTA / Web Server**: Upload the `.bin` file via the built-in web server or check for updates wirelessly in **Settings -> System -> Check for Updates**.
