# Witchling Reader User Guide

Welcome to **Witchling Reader** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [Witchling Reader User Guide](#witchling-reader-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
    - [Moving through lists](#moving-through-lists)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Recent Books Screen](#34-recent-books-screen)
    - [3.5 Book Info Screen](#35-book-info-screen)
    - [3.6 File Transfer Screen](#36-file-transfer-screen)
    - [3.7 Settings](#37-settings)
      - [3.7.1 Display](#371-display)
      - [3.7.2 Reader](#372-reader)
      - [3.7.3 Controls](#373-controls)
      - [3.7.4 System](#374-system)
      - [3.7.5 OPDS Servers (Multiple Libraries)](#375-opds-servers-multiple-libraries)
      - [3.7.6 Web Settings (WiFi + OPDS)](#376-web-settings-wifi--opds)
    - [3.8 Sleep Screen](#38-sleep-screen)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Chapter Selection Screen](#5-chapter-selection-screen)
  - [6. Current Limitations \& Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues \& Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)


## 1. Hardware Overview

The device utilises the standard buttons on the Xteink X4 (in the same layout as the manufacturer firmware, by default):

### Button Layout
| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Volume Up**, **Volume Down**, **Reset** |

Button layout can be customized in the **[Controls Settings](#373-controls)**.

### Moving through lists

The firmware's lists share one consistent set of button controls: the **OPDS catalog** and the **OPDS servers** with their settings, **Settings** and the **reader menu**, the option lists and pickers they open (fonts, dictionaries, language, keyboard layouts), the menus (file options, quick overrides), **File Transfer**'s choice of mode, **Customise Status Bar**, and the screen shown when you finish a book.

| Button | Press | Hold |
| --- | --- | --- |
| **Up / Down** | Move one row; tap twice quickly to jump a page | Jump to the first / last row (in Settings and the reader menu: switch to the previous / next tab) |
| **Left / Right** | Move one row, or the action the screen shows | Page back / forward; keep holding to keep paging |
| **Confirm** | Open the selected row | The same as a press |
| **Back** | Go back | Return to the Home screen (see below) |

A page jump moves the list by a screenful and keeps the selection on the same line of the screen. On the last screen it goes to the last row, and on the first screen to the first row.

The hints show which is which. **«** on the Left box and **»** on the Right box mean *hold to page*. The word after the arrow is what a short press does: **« Up** / **» Down** where Left and Right move the selection, or the screen's own action, such as **« Search** / **» Info**. A box showing only **«** or **»** has no action for the selected row right now. A short press does nothing there, and holding it still pages.

Holding **Back** never throws away a change. Settings saves before going Home, and the reader menu closes the book as its **Go Home** item does, keeping the menu's changes. Inside a book, the lists opened over it (quick overrides, the pickers in the reader menu) and a file's options menu treat a held Back as a press.

A button action you set yourself under **Settings → Controls** comes first: give a long Left or Right press an action of its own and holding it no longer pages, and a short-press action on the page-turn keys (Up/Down) runs as well as the move.

### Taking a Screenshot
When the Power Button and Volume Down button are pressed at the same time, it will take a screenshot and save it in the folder `screenshots/`.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.
---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.
In the **[Controls Settings](#373-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE]
> On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. It shows the most recently read book as a cover thumbnail and provides navigation to **[Reading Mode](#4-reading-mode)**, the **[Browse Files](#33-browse-files-screen)** screen, the **[Recent Books](#34-recent-books-screen)** screen, the **[File Transfer](#36-file-transfer-screen)** screen, and **[Settings](#37-settings)**.

**Correcting a book's title or author:** a metadata file next to an EPUB with the same name (`Some Book.opf` beside `Some Book.epub`) takes precedence over the details inside the book. The metadata editor plugin in the web interface writes one for you. The Home screen and Recent Books notice when that file is added, changed or removed, over USB or from the plugin, and show the new details without the book having to be opened first.

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen is a full-featured browser for your books and the folders they are in. It lists the books the reader can open (EPUB, XTC/XTCH, TXT and Markdown) and nothing else: a cover image or `.opf` file saved beside a book (see [sidecar files](docs/sidecar-files.md)) is not listed on its own, so a book downloaded from an OPDS catalogue shows up once. Images and every other file are in **Settings → System → Tools → All Files**.

* **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up and down through folders and books. Long-pressing these buttons scrolls a full page at a time.
* **Open Selection:** Press **Confirm** to open a folder or read a selected book.
* **Options menu:** Hold **Right** (the page-forward button) to open the menu for the selected item. In a folder short enough to fit on one screen, a short press of Right opens it too. On a device without a Confirm key, such as the X4 Pro, **Confirm** opens the menu instead, and a tap on a row opens the item. The button hints always show which button does what.
  * For a book: **Open**, **Mark as read**, **Info**, **Delete Book Cache**, **Remove**, **Move to folder**, **New Folder**, **Search** and **Search all folders**.
  * For a folder: **Open**, the sort and visibility options, **Search**, **Search all folders**, **New Folder** and **Remove**, which deletes the folder and everything in it.

#### Book view

**Book view** in the options menu switches how books are shown:

* **Filenames** (the default) lists each book by its filename.
* **Details** shows each EPUB's title from its metadata, with the author and series underneath (for example "Thomas Mann · Werke #3"), and how far you have read on the right: a percentage, or **Finished**. An `.opf` metadata file beside the book overrides what is inside the book, as everywhere else. TXT, Markdown and XTC books keep their filename, with the percentage.

* **Covers** shows the folder as a grid of covers, with the title and author under each. Each cover is shown whole, scaled to fit, never cropped. A finished book has a folded corner and one you are reading a bar along the bottom. A folder appears as a folder the size of a cover, with the number of books in it and in all the folders below it (up to "999+"; "..." while they are being counted). The grid holds as many covers as the screen has room for: two rows of two on the X3 and X4, three of three on the LilyGo T5S3. **Up**/**Down** move a row and **Left**/**Right** one cover, wrapping round at the ends, and the page follows the selection. On a touch screen a tap selects a cover, a second tap opens it, and a swipe turns the page. Hold **Right** for the options menu: its hint reads **Right / Options**.
  * A book whose cover has not been made yet shows a card with its title at first. The covers of the page on screen are then made one at a time, and each appears as soon as it is ready. Pressing a button pauses this, so the screen stays responsive. A large cover can take several seconds. A cover is made once and kept, and Recent Books and the "book finished" screen use the same ones. A book with no cover at all keeps its title card; if making a cover fails, it is tried again the next time you open the folder.

The first time a folder shows an EPUB you have never opened, its row or card shows the filename for a moment while the book is read. The screen then redraws once with the details, and after that they come up at once. Sorting and **Search** still go by filename, and **Search all folders** lists its results by filename in every view.

#### Sorting

Files and folders can be sorted by **name**, **date**, **size**, or **type**, in either ascending or descending order. The sort order is set from the options menu.

#### Organising files

* **New Folder** makes a folder inside the one you are browsing. A name the SD card cannot hold is corrected rather than refused.
* **Move to folder** opens a folder picker that shows folders only. Browse to where the file should go: the move puts it in the folder you are *browsing*, which the header names in full, not in the row under the highlight. Press **Right** (the page-forward button; labelled **Move**) to move it there, or **Left** (the page-back button; labelled **New**) to make a new folder first. **Back** cancels.
* A move is instant whatever the size of the file, because nothing is copied. It never overwrites anything: if a file of the same name is already there, or the file is already in that folder, the move is refused and the reason shown.
* A book's cover image and `.opf` file go with it when you move or remove the book here, since this screen does not list them. In **All Files** each file is moved or removed on its own.
* **Remove** on a folder deletes it and everything in it. It sits last in the menu because it cannot be undone.

#### Searching

* **Search** narrows the folder you are in to the names that contain what you type (upper and lower case are treated the same). The search text is shown in the header.
* **Search all folders** searches this folder and every folder below it, and lists the matches with their path, so two books with the same name can be told apart. Opening a result opens it where it lives. **Go to folder** opens the folder a result is in, with the file selected.
* **Back** ends a search; so does changing folder. **Clear search** in the folder's menu does the same.
* The card is searched when you ask rather than indexed in advance, so results always reflect what is on the card, including anything copied over USB. **Show Hidden Files** applies to search as it does to browsing.

#### Large folders

Folders with many entries are handled via an SD-card-backed index so memory use stays bounded regardless of folder size.

### 3.4 Recent Books Screen

The Recent Books screen lists the books you opened last, newest first, from wherever they are on the card. Selecting a book opens it at the last read position, and a long **Confirm** on an EPUB fetches your KOReader progress first, as in Browse Files.

It is Browse Files over that list, with the same three views — **Covers** (the default), **Details** and **Filenames** — chosen in the options menu under **Book view**. Recent Books remembers its own choice, so changing it here leaves Browse Files as it was. The keys are Browse Files' too: in Covers, **Up**/**Down** move a row and **Left**/**Right** one book, wrapping round at the ends; hold **Right** for the options menu. **Back** returns to the Home screen.

The options menu has what you can do with the selected book: open it, mark it as read, show its details, delete its cache, **Remove from recents** (it stays on the card) and **Go to folder**, which opens Browse Files in the folder the book is in.

### 3.5 Book Info Screen

The Book Info screen shows full metadata for a book: cover image, title, author, and description (paged if long). It is accessible from the context menu in Browse Files or from the reader menu while reading.

### 3.6 File Transfer Screen

The File Transfer screen provides multiple ways to manage and transfer e-books:

- **Join a Network**: Connect to an existing WiFi network (STA mode) and host a local web server for browser-based file management and uploads.
- **Create Hotspot**: Create a standalone WiFi Access Point (AP mode) on the device, allowing phones or laptops to connect directly and upload books without an existing network.
- **OPDS Browser**: Open the on-device OPDS browser to browse and download books directly from your configured OPDS catalogs (such as Calibre, Kavita, or public catalogs).

See the [webserver docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [webserver docs](./docs/webserver.md) for details.

### 3.7 Settings

The Settings screen is organized into four top-level tabs: **[Display](#371-display)**, **[Reader](#372-reader)**, **[Controls](#373-controls)**, and **[System](#374-system)**.

#### Tab Navigation
- **Switch Between Tabs & List:** While browsing settings rows, pressing **Back** returns focus to the top tab bar.
- **Change Tab:** With the tab bar focused, press **Confirm** to advance to the next tab, or use **Left** / **Right** (or side buttons, depending on orientation) to cycle between tabs.
- **Enter Tab / Toggle Setting:** Pressing **Up** / **Down** steps from the tab bar down into the category's settings. Pressing **Confirm** on a setting row toggles its value or opens its dedicated picker/submenu.

The settings are grouped into tabs. The screen opens on the tab bar: **Confirm** moves to the next
tab, **Down** enters its list, and **Back** on a row returns to the bar (on the bar it saves and
returns Home). **Hold Up / Down** to switch to the previous / next tab: it opens where you left it.
Moving within a tab follows **[Moving through lists](#moving-through-lists)**.

#### 3.7.1 Display

- **Time to Sleep**: Slider from 0 (Never) to 60 minutes; sets the inactivity period before the device sleeps.
- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  - "Dark" (default) - The Witchling Reader logo on a dark background
  - "Light" - The same logo on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#38-sleep-screen) for more information
  - "Cover" - The cover of the currently open book
  - "None" - A blank screen
  - "Cover + Custom" - Book cover with fallback to Custom behavior
  - "Page Overlay" - A transparent PNG composited over the current reader page (book content shows through)
  - "Quick Resume" - A minimal screen that resumes reading immediately on wake
- **Sleep Screen Cover Mode**: How to display the cover image:
  - "Fit" (default) - Scale to fit, white borders
  - "Crop" - Scale and crop to fill the screen
- **Sleep Screen Cover Filter**: Filter applied to the cover image:
  - "None" (default) - Grayscale
  - "Contrast" - Black & white without grayscale conversion
  - "Inverted" - Inverted black & white
- **Sleep Screen Overlay**: Tint overlay applied on top of the sleep image (useful for dimming a cover or overlay image):
  - "Off" (default), "White", "Gray", "Black"
- **Sleep Image Pick Mode**: How to cycle through images in the Custom sleep screen:
  - "Random" (default) - Pick a random image each time
  - "Sequential" - Cycle through images in order
- **Quick Resume Timeout**: Whether the Quick Resume sleep screen auto-clears on next wake.
- **Hide Battery %**: Where to suppress the battery percentage in the status bar:
  - "Never" (default), "In Reader", "Always"
- **Refresh Frequency** (submenu): Settings for screen refresh behaviour while reading:
  - **Refresh Frequency** - Slider (0 = Never, up to 60) for how often a full refresh runs to clear ghosting
  - **Refresh After Image Pages** - Whether to do an extra refresh after pages containing images
- **Sunlight Fading Fix**: Software fix for white X4 models that may fade in direct sunlight. "OFF" (default) / "ON".

#### 3.7.2 Reader

- **Reading Orientation**: Screen orientation for reading:
  - "Portrait" (default), "Landscape CW", "Inverted", "Landscape CCW"

**EPUB Font** (submenu):
- **Font Family**: Font used for EPUB reading. Includes built-in fonts (Bookerly, Noto Sans) plus any fonts installed on the SD card (which are memory-mapped directly from storage for minimal RAM usage).
- **Font Size**: 10pt to 26pt. 10pt to 20pt are typefaces designed at that size; 22, 24 and 26pt are the 20pt face enlarged.
- **Text Anti-Aliasing**: Smooth grey edges on text. Slows page turns slightly. "ON" / "OFF"
- **Text Darkness**: Ink density for rendered text: "Normal" (default), "Dark", "Extra Dark", "Max Dark"

**Layout** (submenu):
- **Paragraph Alignment**: "Justified" (default), "Left", "Center", "Right", "Book Style"

**Spacing** (submenu):
- **Screen Margin**: Left/right margin in Reading Mode, 5–40 px in 5 px steps.
- **Line Spacing**: "Tight", "Normal" (default), "Wide"
- **Extra Paragraph Spacing**: "ON" adds vertical space between paragraphs; "OFF" uses first-line indentation instead.

**Images** (submenu):
- **Images**: "Display" (default), "Placeholder" (show a box where the image would be), "Suppress" (skip images entirely)
- **Large Image Placeholder**: Whether to substitute an explicit placeholder for images that are too large to display inline. "ON" / "OFF"

- **Embedded Style**: Use the EPUB's own HTML/CSS styling. "ON" (default) / "OFF"
- **Hyphenation**: Automatic hyphenation while reading. "ON" / "OFF"
- **Synthetic TOC Fallback**: Generate a table of contents from headings when the EPUB has an invalid or missing TOC. "ON" / "OFF"
- **Customise Status Bar**: Opens a submenu to configure the reading status bar: location (Top / Bottom), content slots for Left, Middle, and Right (Battery, Page Count, Percentage, Pages & %, Chapter Title, Book Title, or Hide), and a single progress bar (Book / Chapter / Hide) that sits at the selected status bar edge.
  The chapter page count and the chapter progress bar cover the whole chapter as the table of contents lists it, even when the book splits that chapter into several files. A `~` before the total means part of it is still an estimate; it firms up as the rest of the chapter is laid out. On/off items are switches; the others step to their next value with **Confirm**. The preview under the list shows the result.

#### 3.7.3 Controls

- **Remap Front Buttons**: Reassign the physical function of each bottom-edge button.
- **Button Actions** (submenus — one per logical button: Back, Confirm, Left, Right, Up/Page Back, Down/Page Forward, Power): For each button, independently configure the **Short Press**, **Double Press**, and **Long Press** action. Available actions include: page forward/back, skip 10 pages, go home, sleep, force refresh, force fast refresh, open TOC, open bookmarks, star page, footnotes, next/previous chapter, exit reader, open reader menu, sync progress, cycle font size, cycle orientation, quick overrides, and ignore.
- **Button Actions Overview**: A read-only overview screen showing the current short/double/long press mapping for every button at a glance.

#### 3.7.4 System

- **Keyboard Layouts**: Which layouts the on-screen keyboard offers, each named in its own language: English (QWERTY), Français (AZERTY), Deutsch (QWERTZ, with ä, ö, ü and ß), Español (with ñ), and ЙЦУКЕН for Русский, Українська, Беларуская and Қазақша. Press **Confirm** on a row to switch it on or off. Until you change anything here, the keyboard offers English.
  - Once two or more layouts are on, the keyboard shows a globe key that switches to the next one.
  - One Latin layout always stays on, because web addresses and passwords need one; its row then reads "Default" and cannot be switched off.
  - On the keyboard, hold **Confirm** on a key for its alternate letter, such as an accented one or Ukrainian ґ and the extra Kazakh letters, or for the other case. Holding **Confirm** on Delete clears the whole text. Shift applies to the next letter only, and `-`, `=`, `.` and `,` are on the symbols page (**?123**).
- **Show Hidden Files**: Show files and folders whose names start with `.`. "ON" / "OFF"
- **Show File Extensions**: Show file extensions in the file browser. "ON" / "OFF"
- **Book view**: How [Browse Files](#33-browse-files-screen) shows books. "Filenames" / "Details" / "Covers"

**Network**:
- **WiFi Networks**: Add, remove, and connect to WiFi networks.
  - To join a network that does not broadcast its name, choose **Add hidden network...** at the end of the list and type its name (SSID). A password saved for that name is reused; otherwise you are asked for one (leave it empty for an open network). While it connects, **Back** abandons the attempt and returns to the list.
  - Whenever the reader needs WiFi it first tries the network it last connected to, then any other saved network in range, strongest first. While it does, **Back** cancels and **Confirm** stops it and shows the network list.
- **OPDS Servers**: Manage OPDS libraries. See [OPDS Servers (Multiple Libraries)](#375-opds-servers-multiple-libraries).


- **All Files**: Browse every file on the SD card, not just books: images, cover and `.opf` files, firmware images and anything else. It works like [Browse Files](#33-browse-files-screen): **Confirm** opens a book or image, and on any other file it opens the menu, where you can **Move to folder** or **Remove** it. Images offer **Set as sleep screen**. Moving or removing a book here moves or removes that file only, not the cover and `.opf` beside it. **Back** at the top folder returns to Settings.


**System**:
- **Clear Reading Cache**: Clear the internal SD card cache.
- **Repair Screen**: Clears ghosting left behind by fast page refreshes, by driving every pixel hard between black and white several times. Takes about 20 seconds and deletes nothing. A maintenance action, not a fix for ghosting while you read.
- **System Information**: Display device info (firmware version, hardware, memory, SD card). When it runs to more than one page, the header shows the page ("1 / 2") and the page buttons move between pages, wrapping round at the ends. How many pages there are depends on the orientation.
- **Boot Diagnostics**: How this boot started, where the last sleep stopped, and the history pairing each sleep with the boot that followed it. One screenful, meant to be photographed into a bug report when the device fails to sleep or fails to wake.

**Firmware Update**:
- **Check for Updates**: Check for and download Witchling Reader firmware updates over WiFi. The download is always checked against the certificate of GitHub's servers, and the firmware against the SHA-256 checksum the release lists, before the reader switches to it. The reader restarts when you leave this screen.
- **SD Firmware Update**: Flash a firmware `.bin` file from the SD card. Press **Confirm** on a file to flash it. The **Options** button hint in the file picker opens the sort and visibility options, **Search**, and **Remove** to delete a `.bin` you no longer need.

#### 3.7.5 OPDS Servers (Multiple Libraries)

Witchling Reader supports saving multiple OPDS servers and switching between them when browsing catalogs.

1. Open **Settings -> System -> OPDS Servers**.
2. Select **Add Server** to create a new entry, or select an existing server to edit it.
3. Configure these fields:
   - **Server Name**: Optional display name (for example, "Home Calibre" or "Public Catalog").
   - **OPDS Server URL**: Full catalog root URL (for Calibre Content Server, usually ends with `/opds`).
   - **Username / Password**: Optional credentials for authenticated servers.
4. Use **Delete Server** inside a server entry to remove it.

Behavior notes:

- You can store up to 8 OPDS servers.
- OPDS authentication supports HTTP Basic auth. If you use Calibre Content Server with authentication enabled, set it to Basic (not Digest).

You can also manage OPDS servers from the web interface while in File Transfer mode:

1. Connect to the device web UI.
2. Open `http://<device-ip>/settings`.
3. Use the **OPDS Servers** card to add, edit, or delete entries.
For web-based WiFi network management, see [Web Settings (WiFi + OPDS)](#376-web-settings-wifi--opds).

**Browsing a catalog.** The catalog follows **[Moving through lists](#moving-through-lists)**.
A short **Left** opens **Search** when the catalog offers one, and a short **Right** opens **Info**
for the selected book. Where either does not apply, that button does nothing on a short press,
and its hint shows only the arrow. Hold **Left** / **Right**, or swipe on a touch screen, to page
through a long catalog; **Up** / **Down** move one row. **Confirm** opens a folder or downloads a
book. When a book comes in several formats, you choose one from a short list that works the same
way.

#### 3.7.6 Web Settings (WiFi + OPDS)

While in **File Transfer** mode, the web settings page includes management cards for both **WiFi Networks** and **OPDS Servers**.

1. On device: open **File Transfer** and connect to WiFi.
1. In a browser, open `http://<device-ip>/settings` or `http://witchling.local`.
1. In **WiFi Networks**, add, edit, or delete saved network entries (SSID + optional password).
1. In **OPDS Servers**, add, edit, or delete OPDS catalogs.

Behavior notes:

- Passwords are never shown back in the web UI after saving.
- Leaving Password blank while editing keeps the existing saved password unchanged.
- The web UI can save hidden-network SSIDs. On the device, join one through **Add hidden network...** in the network list; its saved password is reused.

### 3.8 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode | Behavior |
|------|----------|
| **Dark** (default) | The Witchling Reader logo on a dark background. |
| **Light** | The Witchling Reader logo on a white background. |
| **Custom** | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found. |
| **Cover** | The cover of the currently open book. Falls back to **Dark** if no book is open. |
| **Cover + Custom** | The cover of the currently open book. Falls back to **Custom** behavior if no book is open. |
| **Page Overlay** | A transparent PNG composited over the current reader page — book content shows through the alpha channel. |
| **Quick Resume** | A minimal screen; waking the device returns to reading immediately. |
| **None** | A blank screen. |

The **Sleep Image Pick Mode** setting controls whether custom images are chosen **randomly** or **sequentially**.

An optional **tint overlay** (Off / White / Gray / Black) can be applied on top of the sleep image to dim or tint it.

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom** or **Cover + Custom**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` or `.png` images inside. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. Used as fallback if no valid images are found in the `.sleep`/`sleep` directory.

> [!TIP]
> For best results:
> - Use PNG (with alpha channel for Page Overlay mode) or uncompressed BMP files with 24-bit color depth.
> - Use a resolution of 480×800 pixels to match the device's screen resolution.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning
| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Volume Up**    |
| **Next Page**     | Press **Right** _or_ **Volume Down** |

The role of the volume (side) buttons can be swapped in the **[Controls Settings](#373-controls)**.

If the **Short Power Button Click** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

### Chapter Navigation
* **Next Chapter:** Press and **hold** the **Right** (or **Volume Down**) button briefly, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Volume Up**) button briefly, then release.

This feature can be disabled in the **[Controls Settings](#373-controls)** to help avoid changing chapters by mistake.


### System Navigation
* **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
* **Return to Browse Files:** Press and hold the **Back** button to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen.
* **Reader Menu:** Press **Confirm** to open the reader menu, which includes: **[Table of Contents](#5-chapter-selection-screen)**, bookmarks, progression sync, quick per-book overrides (font, images, hyphenation, bionic reading…), take screenshot, and reader settings. Its entries are grouped into tabs. While the tab bar is selected, **Confirm** moves to the next tab, and its button hint names that tab. **Hold Up / Down** to switch tabs: each opens where you left it. **Hold Back** to close the book and return Home, as **Go Home** does; changes made in the menu are kept.
* **Your place is kept by paragraph as well as by page.** If a book is laid out differently the next time you open it, for example after you changed the font size from outside the book or after a firmware update re-indexed it, it opens at the paragraph you were reading rather than at a page number scaled to the new length.

### Supported Languages

Witchling Reader renders text using the following Unicode character blocks, enabling support for a wide range of languages:

*   **Latin Script (Basic, Supplement, Extended-A):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, and others.
*   **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.

What is not supported: Chinese, Japanese, Korean, Vietnamese, Hebrew, Arabic, Greek and Farsi.

---

## 5. Chapter Selection Screen

Accessible by pressing **Confirm** while inside a book and selecting **Table of Contents**.

1.  Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to highlight the desired chapter.
2.  Press **Confirm** to jump to that chapter.
3.  *Alternatively, press **Back** to cancel and return to your current page.*

---

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features have known limitations:

* **Cover Images:** Large cover images embedded into EPUB require several seconds (~10s for ~2000 pixel tall image) to convert for the sleep screen and home screen thumbnail. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.
* **Right-to-left scripts (Hebrew, Arabic):** Not currently supported. For BiDi / RTL support, use the original [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) firmware.
* **CJK (Chinese, Japanese, Korean):** Not supported. See https://github.com/aBER0724/crosspoint-reader-cjk for a CJK-capable fork.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Witchling Reader:

- If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot cleanly to the Home Screen.
- If there are issues with broken cache or corrupted configuration, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.json`, `state.json`, or `epub_*` cache directories inside `.crosspoint/`).
- For locked X4 hardware, the USB port operates purely for power/charging; flashing is performed via SD card (`update.bin`) or Wi-Fi OTA. USB serial data connection, logging, and monitoring are stripped from the firmware to maximize boot speed, eliminate background overhead, and save power.

