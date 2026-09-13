# Witchling Reader

A personal fork of [jpirnay/witchhunt-reader](https://github.com/jpirnay/witchhunt-reader) exclusively targeting the locked **Xteink X4**.

For full documentation refer to the [upstream README](https://github.com/jpirnay/witchhunt-reader#readme).

---

## Changes in this Fork

- [OPDS Progression 1.0](https://github.com/opds-community/drafts/blob/main/opds-progression-1.0.md) Sync
- Removed TXT and Markdown Support
- Lyra Sole Theme
- Removed KOReader sync and Calibre wireless transfer
- Removed Weather Integration
- Removed USB Serial, Flashing & Host Communication
- Removed Reading Statistics
---

## Building

Requires [PlatformIO Core](https://platformio.org/install/cli) (`pio`) — or VS Code with the PlatformIO IDE extension.

```sh
# Clone with submodules (or run `git submodule update --init --recursive` after cloning without)
git clone --recursive <repo-url> witchling-reader
cd witchling-reader

# Enable repo-managed git hooks (once per clone)
git config core.hooksPath .githooks && chmod +x .githooks/pre-commit

# Build firmware binary (.pio/build/x4/firmware.bin)
pio run

# Build release binary
pio run -e gh_release
```

## Installing Firmware

> [!NOTE]
> Locked X4 hardware does not support flashing over USB

To install or update firmware on the device:

- **SD Card Update**: Copy the compiled binary (`.pio/build/x4/firmware.bin`) to the SD card root as `update.bin`, or navigate to **Settings -> System -> SD Firmware Update**.
- **Wi-Fi OTA / Web Server**: Upload the `.bin` file via the built-in web server or check for updates wirelessly in **Settings -> System -> Check for Updates**.

For device documentation, hardware specifications, and troubleshooting, refer to the [upstream README](https://github.com/jpirnay/witchhunt-reader#readme) and the `docs/` directory.
