# Witchling Reader

A lightweight, streamlined fork of [jpirnay/witchhunt-reader](https://github.com/jpirnay/witchhunt-reader) tailored exclusively for the locked **Xteink X4** (physical buttons, no touch, no PSRAM).

For full original documentation, refer to the [upstream README](https://github.com/jpirnay/witchhunt-reader#readme).

---

## Changes in this Fork

### Added
- **OPDS Progression 1.0**: Automatically syncs reading progress with compatible OPDS servers (e.g., Kavita, Audiobookshelf).
- **Quick Resume**: Press the Back button on the home screen to instantly jump back into your current book.
- **Simplified Status Bar**: Streamlined status bar customization options in Settings.
- **OPDS in File Transfer**: Access your OPDS library directly from the File Transfer menu.
- **Enhanced Tab Navigation**: Bottom buttons and dynamic tab labels make navigating reader menus faster.

### Removed
- **TXT and Markdown Reader**: Focused exclusively on an optimized, fast EPUB experience.
- **Unused Subsystems**: Removed Clock, Weather integration, and Reading Statistics to save memory and battery.
- **KOReader Sync & Calibre Wireless Transfer**: Simplified the networking stack in favor of direct OPDS sync.
- **USB Serial & Flashing Code**: Removed unusable USB serial, host, and flashing features for locked X4 hardware.
- **Extra Themes & Touch Code**: Kept Lyra as the sole theme with borderless button hints; removed touch layer overhead.
- **Non-English Translations**: Pruned non-English languages to keep firmware size lean and fast.
- **Unused Features**: Removed Bionic Reading, Guide Dots, Auto Page Turn, Page as QR Code, and Screenshots.
- **Font Scaling Test**: Removed the developer font scaling test screen from System settings.
- **UI Font Size & Unused Font Data**: Locked UI typography to Normal default and removed unused `inter_ui_14` font bitmaps to reclaim ~109 KB flash.
- **Busy Indicator**: Removed the "Show Busy Indicator" setting and disabled transition screen overlays.
- **Dead Code & Unused Assets**: Purged orphaned UI components (`CardLayout`, `FrontlightPanelActivity`, unused icons) and pruned 229 unreferenced translation strings.

### Fixed
- **EPUB Cover Detection**: Fixed home screen sometimes displaying wrong images (like title banners or logos) by detecting covers from the Table of Contents, cover pages, and prioritizing real cover art.
- **Reader Menu Navigation**: Fixed bottom button navigation issues inside reader menus.

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
