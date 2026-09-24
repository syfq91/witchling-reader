#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <cstdio>
#include <iosfwd>
#include <string>
#include <vector>

class CrossPointSettings {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  // Static instance
  static CrossPointSettings instance;

 public:
  // Delete copy constructor and assignment
  CrossPointSettings(const CrossPointSettings&) = delete;
  CrossPointSettings& operator=(const CrossPointSettings&) = delete;

  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    BLANK = 4,
    COVER_CUSTOM = 5,
    OVERLAY = 6,
    QUICK_RESUME = 7,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_IMAGE_PICK_MODE { PICK_RANDOM = 0, PICK_SEQUENTIAL = 1, SLEEP_IMAGE_PICK_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    // Greyscale like NO_FILTER, but stretches the image's own tonal range first.
    ADAPTIVE_TONE = 3,
    // Greyscale too, but levels via the histogram's own CDF rather than its endpoints.
    // Reaches bimodal images that ADAPTIVE_TONE leaves flat, at the cost of more dither
    // texture in smooth gradients -- see AdaptiveTone.h.
    EQUALIZE_TONE = 4,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };
  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  enum STATUS_BAR_PROGRESS_BAR {
    BOOK_PROGRESS = 0,
    HIDE_PROGRESS = 1,
    STATUS_BAR_PROGRESS_BAR_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR_THICKNESS {
    PROGRESS_BAR_THIN = 0,
    PROGRESS_BAR_NORMAL = 1,
    PROGRESS_BAR_THICK = 2,
    STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT
  };
  enum STATUS_BAR_TITLE { BOOK_TITLE = 0, CHAPTER_TITLE = 1, HIDE_TITLE = 2, STATUS_BAR_TITLE_COUNT };
  enum STATUS_BAR_POSITION {
    STATUS_BAR_TOP = 0,
    STATUS_BAR_BOTTOM = 1,
    STATUS_BAR_POSITION_COUNT
  };
  // Compatibility aliases
  using STATUS_BAR_ITEMS_POSITION = STATUS_BAR_POSITION;
  static constexpr uint8_t STATUS_BAR_ITEMS_TOP = STATUS_BAR_TOP;
  static constexpr uint8_t STATUS_BAR_ITEMS_BOTTOM = STATUS_BAR_BOTTOM;

  enum STATUS_BAR_SLOT_CONTENT {
    SLOT_HIDE = 0,
    SLOT_BATTERY = 1,
    SLOT_PAGE_COUNT = 2,
    SLOT_BOOK_PERCENTAGE = 3,
    SLOT_PAGE_AND_PERCENTAGE = 4,
    SLOT_CHAPTER_TITLE = 5,
    SLOT_BOOK_TITLE = 6,
    STATUS_BAR_SLOT_CONTENT_COUNT
  };

  enum ORIENTATION {
    PORTRAIT = 0,       // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,   // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,       // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3,  // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName)
  enum FONT_FAMILY { BOOKERLY = 0, NOTOSANS = 1, FONT_FAMILY_COUNT };
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Font size options, in ASCENDING PIXEL ORDER, which is also the order the settings UI shows
  // them in -- a row's option index IS its stored value everywhere (device selector, web API,
  // settings JSON), so the two cannot be allowed to disagree.
  //
  // These values are PERSISTED and they were NOT always in this order. PT_10 was appended as 4
  // and sat between PT_18 and nothing, so the picker read "Small Medium Large X-Large
  // Tiny". That was tolerable while the labels were adjectives; it is plainly broken once they
  // read "12pt 14pt 16pt 18pt 10pt". Rather than add an index-to-value indirection for the sake
  // of one misplaced entry, the values are renumbered and old files are migrated on load -- see
  // FONT_SIZE_ORDER_VERSION and remapLegacyFontSize().
  //
  // Named by POINT SIZE, not by adjective. The adjectives were never accurate -- "medium" was
  // whatever 14 pt happened to be -- they ran out at "extra large" and degenerated into XX_LARGE,
  // and the labels a reader sees have been point sizes since the UI stopped guessing at names.
  // Only the VALUES are persisted, never the identifiers, so the rename cost nothing.
  //
  // PT_22/24/26 have no faces of their own: they render the 20 pt master scaled (see
  // GfxRenderer::insertScaledFont). In every other respect they are ordinary rungs -- selectable
  // as the default size, available as a per-book override, and cached separately because each
  // carries its own font ID.
  enum FONT_SIZE {
    PT_10 = 0,
    PT_12 = 1,
    PT_14 = 2,
    PT_16 = 3,
    PT_18 = 4,
    PT_20 = 5,
    PT_22 = 6,
    PT_24 = 7,
    PT_26 = 8,
    FONT_SIZE_COUNT
  };

  /// Bumped when FONT_SIZE values are renumbered. A settings or recent-books file stamped lower
  /// than this holds values from the older numbering and is remapped as it loads.
  ///
  /// 1 = PT_10 moved from 4 to 0 and everything below it shifted up one, so the enum runs in
  /// pixel order.
  static constexpr uint8_t FONT_SIZE_ORDER_VERSION = 1;

  /// A persisted FONT_SIZE from a file stamped `fileVersion`, in today's numbering.
  ///
  /// Out-of-range input is returned untouched rather than guessed at: it is either a
  /// hand-edited file or a value from a firmware newer than this one, and both are better left
  /// for the caller's own clamp to deal with.
  static constexpr uint8_t remapLegacyFontSize(const uint8_t stored, const uint8_t fileVersion) {
    if (fileVersion >= FONT_SIZE_ORDER_VERSION) return stored;
    // v0: PT_12=0 PT_14=1 PT_16=2 PT_18=3 PT_10=4. PT_20 did not exist.
    switch (stored) {
      case 0:
        return PT_12;
      case 1:
        return PT_14;
      case 2:
        return PT_16;
      case 3:
        return PT_18;
      case 4:
        return PT_10;
      default:
        return stored;
    }
  }

  /// The reader size ladder: every rung in ASCENDING PIXEL order, paired with the point size its
  /// faces are generated at.
  ///
  /// ONE table because there were four — this list, a copy in getTallerBuiltinReaderFontId(), the
  /// point-size map in SdCardFontSystem.cpp, and the pair of arrays in
  /// EpubReaderActivity::buildReaderFontSizeLadder(). Adding PT_20 meant updating all of them,
  /// and each failed differently and silently when missed: "one size bigger" would skip the new
  /// rung, SD fonts would load the wrong point size, the heading ladder would ignore it.
  ///
  /// Rung order and enum value now agree, which is what lets the settings UI use the value as an
  /// option index. Keep them in step: a rung inserted in the middle needs an
  /// FONT_SIZE_ORDER_VERSION bump and a remapLegacyFontSize() case, exactly as PT_10 did.
  ///
  /// Adding a size at the TOP is: append a rung here, add the case to getBuiltinReaderFontId(),
  /// generate the faces, register them in main.cpp.
  struct ReaderFontRung {
    uint8_t size;    ///< a FONT_SIZE value
    uint8_t points;  ///< the point size its faces are generated at
  };
  static constexpr ReaderFontRung FONT_SIZE_RUNGS[] = {
      {PT_10, 10}, {PT_12, 12}, {PT_14, 14}, {PT_16, 16}, {PT_18, 18},
      {PT_20, 20}, {PT_22, 22}, {PT_24, 24}, {PT_26, 26},
  };
  static constexpr int FONT_SIZE_RUNG_COUNT = static_cast<int>(sizeof(FONT_SIZE_RUNGS) / sizeof(FONT_SIZE_RUNGS[0]));

  /// The point size a FONT_SIZE renders at, or 0 if it names no rung.
  static constexpr uint8_t fontSizePoints(const uint8_t size) {
    for (const ReaderFontRung& r : FONT_SIZE_RUNGS) {
      if (r.size == size) return r.points;
    }
    return 0;
  }
  /// Every FONT_SIZE value must name exactly one rung. The settings UI indexes its label list by
  /// enum VALUE, so a value with no rung would render as a blank, selectable row.
  static constexpr bool ladderCoversEveryFontSize() {
    for (int v = 0; v < FONT_SIZE_COUNT; ++v) {
      if (fontSizePoints(static_cast<uint8_t>(v)) == 0) return false;
    }
    return true;
  }

  /// What a font-size row displays: the point size itself, "14pt".
  ///
  /// Not "Tiny/Small/Medium/Large/X-Large" any more. Those stopped carrying information once the
  /// ladder reached six rungs -- there is no honest adjective after "extra large" -- and they were
  /// hand-listed as StrId vectors in four separate places, indexed by enum value, which is the
  /// same shape as the point-size copy that had already gone stale. Deriving the label from
  /// FONT_SIZE_RUNGS means changing a rung is one edit. It also just tells a reader who needs
  /// 20 pt what they are choosing.
  ///
  /// Empty if `size` names no rung. Untranslated: the numeral carries the meaning and "pt" is the
  /// unit in every locale this ships with.
  ///
  /// Inline for the same reason stepFontSize() is: the ladder is exactly the kind of thing a host
  /// test should hold still, and linking the NVS half of CrossPointSettings.cpp to reach it would
  /// mean it never got one.
  static std::string fontSizeLabel(const uint8_t size) {
    const uint8_t pt = fontSizePoints(size);
    if (pt == 0) return {};
    char buf[8];
    snprintf(buf, sizeof(buf), "%upt", static_cast<unsigned>(pt));
    return buf;
  }

  /// The whole label list for a font-size row, ready to assign to SettingInfo::enumLabels.
  ///
  /// Indexed by enum VALUE, which since the renumbering is also ladder order. `defaultLabel`, when
  /// given, is the "Default" entry the per-book override rows carry at index 0, which shifts
  /// every real value up by one.
  static std::vector<std::string> fontSizeLabels(const char* const defaultLabel = nullptr) {
    const size_t shift = defaultLabel ? 1 : 0;
    std::vector<std::string> labels(static_cast<size_t>(FONT_SIZE_COUNT) + shift);
    if (defaultLabel) labels[0] = defaultLabel;
    // By enum VALUE. ladderCoversEveryFontSize() guarantees that leaves no slot empty.
    for (const ReaderFontRung& r : FONT_SIZE_RUNGS) labels[r.size + shift] = fontSizeLabel(r.size);
    return labels;
  }

  // `size` moved `delta` steps along the ladder and clamped at both ends. Clamped
  // rather than wrapped: a pinch that has reached the largest size should stay
  // there, not jump to the smallest.
  //
  // Inline so it can be exercised on the host without dragging in the NVS half
  // of CrossPointSettings.cpp — the ladder order is exactly the kind of thing a
  // test should hold still.
  static constexpr uint8_t stepFontSize(const uint8_t size, const int delta) {
    int idx = -1;
    for (int i = 0; i < FONT_SIZE_RUNG_COUNT; ++i) {
      if (FONT_SIZE_RUNGS[i].size == size) {
        idx = i;
        break;
      }
    }
    // An unrecognised value (a hand-edited settings file) has no place on the
    // ladder; leave it alone rather than guessing which end it belongs at.
    if (idx < 0) return size;
    int target = idx + delta;
    if (target < 0) target = 0;
    if (target > FONT_SIZE_RUNG_COUNT - 1) target = FONT_SIZE_RUNG_COUNT - 1;
    return FONT_SIZE_RUNGS[target].size;
  }
  enum LINE_COMPRESSION { TIGHT = 0, NORMAL = 1, WIDE = 2, LINE_COMPRESSION_COUNT };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Size of the menu/chrome text. Each step rebinds the three logical UI font IDs one rung up
  // the Inter ladder (applyUiFontScale() in main.cpp) and adds the matching number of pixels to
  // every metric that has to hold a line of UI text (UiFontLadder::applyTo() in UiFontScale.h).
  //
  // Deliberately does NOT touch the reader's own font or its status bar: those feed the text
  // viewport, and changing the viewport invalidates every book's pagination cache. This setting
  // is about making menu rows easier to hit, not about re-laying-out books.
  enum UI_FONT_SIZE { UI_FONT_SIZE_DEFAULT = 0, UI_FONT_SIZE_LARGE = 1, UI_FONT_SIZE_COUNT };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  // File browser sort mode (per-session, not persisted)
  enum FILE_SORT_MODE { SORT_BY_NAME = 0, SORT_BY_DATE = 1, SORT_BY_SIZE = 2, SORT_BY_TYPE = 3, FILE_SORT_MODE_COUNT };

  // File browser sort direction (per-session, not persisted)
  enum FILE_SORT_DIRECTION { SORT_ASCENDING = 0, SORT_DESCENDING = 1, FILE_SORT_DIRECTION_COUNT };

  // Action mapped to each tilt gesture direction.
  enum TILT_GESTURE_ACTION {
    TILT_ACT_NONE = 0,
    TILT_ACT_NEXT_PAGE = 1,
    TILT_ACT_PREV_PAGE = 2,
    TILT_GESTURE_ACTION_COUNT
  };

  // Whether a list-row tap activates immediately or first moves selection.
  enum TOUCH_LIST_ACTIVATION {
    TOUCH_LIST_SELECT_THEN_ACTIVATE = 0,
    TOUCH_LIST_ACTIVATE_IMMEDIATELY = 1,
    TOUCH_LIST_ACTIVATION_COUNT
  };

  // Text darkness for AA glyph rendering (forwarded to GfxRenderer::setTextDarkness)
  enum TEXT_DARKNESS {
    DARKNESS_NORMAL = 0,      // true 4-level AA
    DARKNESS_DARK = 1,        // historical default — bolder dark-gray AA
    DARKNESS_EXTRA_DARK = 2,  // both AA shades pushed to darkest gray state
    DARKNESS_MAXIMUM = 3,     // skip grayscale passes — AA pixels stay solid black from BW pass
    // Lighter than NORMAL: both AA shades take the LIGHT tone instead of their
    // own. Appended rather than placed at 0, where it belongs on a darkness
    // scale, because this value is PERSISTED — inserting would silently turn
    // every saved DARKNESS_DARK into DARKNESS_NORMAL and so on down the list.
    // The menu therefore reads Normal, Dark, Extra Dark, Maximum, Lighter.
    DARKNESS_LIGHT = 4,
    TEXT_DARKNESS_COUNT
  };
#ifdef ENABLE_IMAGE_DITHERING_EXTENSION
  enum IMAGE_DITHERING {
    IMAGE_DITHER_BAYER = 0,
    IMAGE_DITHER_ATKINSON = 1,
    IMAGE_DITHER_DIFFUSED_BAYER = 2,
    IMAGE_DITHERING_COUNT
  };
#else
  enum IMAGE_DITHERING { IMAGE_DITHER_BAYER = 0, IMAGE_DITHERING_COUNT };
#endif


  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Apply information overlay with reading progress on sleep cover
  uint8_t sleepCoverOverlay = 0;
  // Sleep image pick mode (random vs sequential walk-through)
  uint8_t sleepImagePickMode = PICK_RANDOM;
  // Quick Resume on Timeout: keep current page on display with a moon icon when sleeping by timeout,
  // and on wake restore the page directly (skipping the boot screen).
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;
  // Status bar settings
  uint8_t statusBarPosition = STATUS_BAR_BOTTOM;
  uint8_t statusBarLeft = SLOT_BATTERY;
  uint8_t statusBarMiddle = SLOT_CHAPTER_TITLE;
  uint8_t statusBarRight = SLOT_PAGE_AND_PERCENTAGE;
  uint8_t statusBarProgressBar = HIDE_PROGRESS;
  // Printed ("physical") page number from the book's page-list. Drawn in parentheses to the left of
  // the device page counter when both share a location; otherwise on its own. Default on.
  uint8_t statusBarPrintedPage = 1;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  uint8_t textAntiAliasing = 1;
  // X3-only: when on, the AA refresh uses the 7-frame community grayscale LUT
  // (~130 ms panel time) instead of the OEM 53-frame LUT (~2.4 s). Mid-tones
  // run slightly darker than X4. Matches what papyrix-reader has shipped since
  // 2025-11. Default on — the 2.2 s/page win dwarfs the subtle mid-tone shift.
  // No effect on X4.
  //
  // JSON key was bumped from "fastAntiAliasing" to "fastAntiAliasingV2" when
  // the default flipped to 1: existing settings files with the old key are
  // ignored, so every device picks up the new C++ default on next load.
  uint8_t fastAntiAliasing = 1;
  // Text darkness (0 = normal, 1 = dark, 2 = extra dark). Default 1 preserves
  // historical AA rendering (both grayscale shades drawn in the MSB pass).
  uint8_t textDarkness = DARKNESS_DARK;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings (EPUB)
  uint8_t fontFamily = BOOKERLY;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // Folder name under /dictionaries (or /.dictionaries) of the StarDict
  // dictionary used for word lookup; empty means no dictionary is selected.
  // A folder NAME, not a path: DictionaryRegistry::resolveBasePath rejects
  // separators and dot prefixes so a hand-edited value cannot escape the roots.
  char dictionaryName[32] = "";
  uint8_t fontSize = PT_14;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout in minutes (0 = never sleep, 1-60).
  uint8_t sleepTimeoutMinutes = 10;
  // Full-refresh frequency in pages (0 = never full-refresh, 1-60).
  uint8_t refreshFrequencyPages = 15;
  // Perform a half refresh on the page immediately following an EPUB page that displayed images.
  uint8_t halfRefreshAfterImagePage = 1;
  uint8_t hyphenationEnabled = 0;
  // Snap near-body font sizes (within ±10% of the body size) to plain body text, so publisher
  // sizing renders native — both inline wrappers like <span style="font-size:0.92em"> around whole
  // paragraphs and a size stated on the block itself (p.body { font-size: 1.1em }). Default on.
  uint8_t fontSizeNormalization = 1;

  // Reader screen margin settings
  uint8_t screenMargin = 5;

  // Extra clearance from the panel edge, on TOP of the board profile's own viewableInsets --
  // see GfxRenderer::setViewablePaddingProvider. The profile says where the case covers the
  // glass; this says how much further in the reader wants content to sit, because on some
  // boards (the T5 S3) the plastic cover comes near enough to the live pixels that text at the
  // profile inset is hard to read.
  //
  // 0 = Narrow, i.e. exactly what the board declares and what every device did before this
  // existed, so the default changes nothing anywhere.
  enum EDGE_MARGIN : uint8_t { EDGE_MARGIN_NARROW = 0, EDGE_MARGIN_MEDIUM, EDGE_MARGIN_LARGE };
  uint8_t edgeMargin = EDGE_MARGIN_NARROW;
  static constexpr int EDGE_MARGIN_STEP_PX = 5;
  // Pixels to inset each edge by. Clamped against a corrupt settings file rather than trusted:
  // this reaches a renderer that has no other opinion about it.
  int getEdgeMarginPadding() const {
    const uint8_t step = edgeMargin <= EDGE_MARGIN_LARGE ? edgeMargin : EDGE_MARGIN_NARROW;
    return step * EDGE_MARGIN_STEP_PX;
  }
  // OPDS download destination folder ("" = SD root). Global; edited from the
  // OPDS server list. Persisted via a category-less SettingInfo::String in
  // SettingsList.h, so it stays out of the on-device Settings screen.
  char opdsDownloadFolder[64] = "";
  // On-disk filename format for OPDS downloads (0=Author-Title default, 1=Title-Author,
  // 2=Title). See OpdsFilenameFormat. Persisted via a category-less SettingInfo::Enum,
  // edited from the OPDS server list; hidden from the on-device Settings screen.
  uint8_t opdsFilenameFormat = 0;
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Acknowledge a press that starts a slow screen change. See ActivityManager::showBusyIndicator().
  uint8_t showBusyIndicator = 1;
  // NOT a preference: the measured cost of a FAST refresh on this panel, carried across boots so
  // the first decision after a reboot is as good as the last one before it. Written from
  // HalDisplay's measurement, never from the UI, and absent from the JSON settings file for that
  // reason. 0 means not yet measured.
  uint16_t measuredFastRefreshMs = 0;
  // Menu/chrome text size (UI_FONT_SIZE)
  uint8_t uiFontSize = UI_FONT_SIZE_DEFAULT;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Show file extensions in the file browser (0 = hidden, 1 = show)
  uint8_t showFileExtensions = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Show a placeholder for large images (>800×600 source pixels) instead of decoding immediately.
  // The user can press OK on the placeholder page to decode the image on demand.
  uint8_t largeImagePlaceholder = 1;
  // Tilt-based page turning (X3 only — requires QMI8658 IMU)
  uint8_t tiltPageTurn = 0;
  // Action when the computed tilt value crosses the positive threshold.
  uint8_t tiltPositiveAction = TILT_ACT_NEXT_PAGE;
  // Action when the computed tilt value crosses the negative threshold.
  uint8_t tiltNegativeAction = TILT_ACT_PREV_PAGE;
  uint8_t touchListActivation = TOUCH_LIST_SELECT_THEN_ACTIVATE;

  // Enable synthetic TOC fallback for malformed/sparse TOC books (1 = enabled, 0 = disabled)
  uint8_t syntheticTocFallback = 1;
  // Expand semantic EPUB footnote references with a short inline preview.
  uint8_t inlineFootnotePreviews = 0;
  // Remove finished book from Recent Books when the end-of-book screen action is selected.
  uint8_t removeFinishedBooksFromRecents = 0;

  // Accept any TLS certificate on https requests (1 = skip validation).
  //
  // For self-hosted servers with a private CA or a self-signed certificate —
  // an OPDS catalog or a local KOReader sync server — where the alternative is
  // that the device simply cannot reach them. It disables authentication of the
  // peer for every https call the firmware makes, so credentials sent to such a
  // server are only as safe as the network path. OTA is deliberately exempt:
  // firmware is executed, so it stays verified whatever this is set to.
  uint8_t skipHttpsValidation = 0;

  // Configurable actions for short / double / long press on each logical button.
  //
  // BTN_DEFAULT means "do the activity's natural behaviour for this button" — it
  // is context-sensitive on purpose (e.g. Confirm-short opens the reader menu but
  // selects an entry in a list), which is why it can't be replaced by baking a
  // fixed action into the defaults. BTN_IGNORE is the opposite: explicitly do
  // nothing and suppress the built-in behaviour for that press.
  //
  // New values MUST be appended before BUTTON_ACTION_COUNT — the settings option
  // list (SettingsList.h btnActionOptions) and persisted JSON map by numeric value.
  enum BUTTON_ACTION {
    BTN_DEFAULT = 0,
    BTN_PAGE_FORWARD,
    BTN_PAGE_BACK,
    BTN_PAGE_FORWARD_10,
    BTN_PAGE_BACK_10,
    BTN_GO_HOME,
    BTN_SLEEP,
    BTN_FORCE_REFRESH,
    BTN_FORCE_FAST_REFRESH,
    BTN_OPEN_TOC,
    BTN_OPEN_BOOKMARKS,
    BTN_STAR_PAGE,
    BTN_FOOTNOTES,
    BTN_NEXT_SECTION,
    BTN_PREV_SECTION,
    BTN_EXIT_READER,
    BTN_READER_MENU,
    BTN_UNUSED_BIONIC_READING,
    BTN_CYCLE_FONT_SIZE,
    BTN_CYCLE_ORIENTATION,
    BTN_QUICK_OVERRIDES,
    BTN_IGNORE,
    BTN_DICTIONARY,
    BTN_SYNC_PROGRESS,
    BUTTON_ACTION_COUNT
  };

  // Short-press actions (default: built-in)
  uint8_t btnShortBack = BTN_DEFAULT;
  uint8_t btnShortConfirm = BTN_DEFAULT;
  uint8_t btnShortLeft = BTN_DEFAULT;
  uint8_t btnShortRight = BTN_DEFAULT;
  uint8_t btnShortPageBack = BTN_DEFAULT;
  uint8_t btnShortPageForward = BTN_DEFAULT;
  uint8_t btnShortPower = BTN_DEFAULT;

  // Double-press actions (default: BTN_DEFAULT = disabled, no disambiguation wait)
  uint8_t btnDoubleBack = BTN_DEFAULT;
  uint8_t btnDoubleConfirm = BTN_DEFAULT;
  uint8_t btnDoubleLeft = BTN_PAGE_BACK_10;
  uint8_t btnDoubleRight = BTN_PAGE_FORWARD_10;
  uint8_t btnDoublePageBack = BTN_DEFAULT;
  uint8_t btnDoublePageForward = BTN_DEFAULT;
  uint8_t btnDoublePower = BTN_DEFAULT;

  // Long-press actions (default: built-in)
  uint8_t btnLongBack = BTN_DEFAULT;
  uint8_t btnLongConfirm = BTN_DEFAULT;
  uint8_t btnLongLeft = BTN_PREV_SECTION;
  uint8_t btnLongRight = BTN_NEXT_SECTION;
  uint8_t btnLongPageBack = BTN_DEFAULT;
  uint8_t btnLongPageForward = BTN_DEFAULT;
  uint8_t btnLongPower = BTN_DEFAULT;

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings& getInstance() { return instance; }

  // How long the power button must be held to put the device to sleep, measured from the
  // press edge in loop(). Device-measured at 401-410 ms of real hold on X3 and X4.
  static constexpr uint16_t getPowerButtonDuration() { return 400; }

  // The same gesture in the other direction, but the wake gate compares against millis()
  // — time since app start, not since the press (see HalGPIO::verifyPowerButtonWakeup).
  // The press that caused the wake began before the app did, so the ROM and second-stage
  // bootloader are already part of the user's hold and nothing charges for them: the real
  // hold is prelude + this value. Shortened from the sleep threshold to claw some of that
  // asymmetry back, and deliberately not shortened further — the gate's job is to reject a
  // stray pocket press, and the prelude is the only margin left doing that.
  static constexpr uint16_t getPowerWakeHoldDuration() { return 300; }
  int getReaderFontId() const;
  // Pure built-in lookup (size enum + family enum -> font ID). Independent of
  // SD-card font selection. Used by the per-book fontFamilyOverride path so
  // an override forces back to a known built-in even when an SD font is the
  // global default.
  static int getBuiltinReaderFontId(uint8_t family, uint8_t size);
  // Heading sizing: return the built-in fontId `stepUp` sizes taller than `size` for
  // `family`, clamped at the largest size. Steps walk the ascending-pixel ladder
  // (PT_10<PT_12<PT_14<PT_16<PT_18), not the FONT_SIZE enum order. `actualStep`
  // (out) receives how many steps were actually taken before the cap (so the caller can
  // compute a residual multiplier when clamped). Returns 0 for unknown families.
  static int getTallerBuiltinReaderFontId(uint8_t family, uint8_t size, uint8_t stepUp, uint8_t* actualStep = nullptr);

  bool saveToFile() const;
  bool loadFromFile();
  void loadStartupFromNvs();
  void saveStartupToNvs() const;

  static void validateFrontButtonMapping(CrossPointSettings& settings);

  // True for an action only the reader can carry out. Such an action must NOT be
  // consumed on another screen: doing so would swallow the input and shadow that
  // screen's own handling of it. Buttons let the event fall through to the
  // activity; gestures decline to claim the contact at all.
  //
  // Everything not named here is global — go home, sleep, the refreshes,
  // bookmarks, the reading light, the touch-navigation switch — and works
  // wherever it is triggered.
  static bool isReaderScopedAction(uint8_t action);

  // Enforce settings whose values depend on others (e.g. sleepScreen=QUICK_RESUME implies
  // quickResumeSleepScreen=ON). Call after any setting mutation that could invalidate the pair.
  static void normalizeDependentSettings(CrossPointSettings& settings);

  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
};

// Outside the class because a constexpr member cannot be called in a static_assert within its
// own definition -- the class is still incomplete there.
static_assert(CrossPointSettings::FONT_SIZE_RUNG_COUNT == static_cast<int>(CrossPointSettings::FONT_SIZE_COUNT) &&
                  CrossPointSettings::ladderCoversEveryFontSize(),
              "every FONT_SIZE must appear exactly once in FONT_SIZE_RUNGS; the settings UI indexes its label "
              "list by enum value and a gap would render as a blank, selectable row");

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
