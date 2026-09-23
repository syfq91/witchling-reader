#include "TocNcxParser.h"

#include <FsHelpers.h>
#include <Logging.h>

#include <algorithm>

#include "../BookMetadataCache.h"
#include "PageListSink.h"

bool TocNcxParser::setup() {
  if (!saxParser_.init(this, startElement, endElement, characterData)) {
    LOG_DBG("TOC", "Couldn't allocate memory for parser");
    return false;
  }
  return true;
}

TocNcxParser::~TocNcxParser() = default;

size_t TocNcxParser::write(const uint8_t data) { return write(&data, 1); }

size_t TocNcxParser::write(const uint8_t* buffer, const size_t size) {
  if (!saxParser_.isActive()) return 0;

  remainingSize -= std::min(size, remainingSize);
  if (!saxParser_.feed(buffer, size)) {
    LOG_DBG("TOC", "Parse error at line %d: %s", saxParser_.errorLine(), saxParser_.errorString());
    return 0;
  }
  if (remainingSize == 0) {
    if (!saxParser_.finalize()) {
      LOG_DBG("TOC", "Parse error (finalize): %s", saxParser_.errorString());
      return 0;
    }
  }
  return size;
}

namespace {
bool isCoverLabel(std::string_view label) {
  while (!label.empty() && isspace(static_cast<unsigned char>(label.front()))) {
    label.remove_prefix(1);
  }
  while (!label.empty() && isspace(static_cast<unsigned char>(label.back()))) {
    label.remove_suffix(1);
  }
  if (label.empty()) return false;

  char lower[64];
  if (label.size() >= sizeof(lower)) return false;
  for (size_t i = 0; i < label.size(); ++i) {
    lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(label[i])));
  }
  lower[label.size()] = '\0';

  if (strcmp(lower, "cover") == 0 || strcmp(lower, "front cover") == 0 ||
      strcmp(lower, "cover page") == 0 || strcmp(lower, "cover image") == 0 ||
      strcmp(lower, "book cover") == 0 || strcmp(lower, "title page") == 0 ||
      strcmp(lower, "titlepage") == 0 || strcmp(lower, "portada") == 0 ||
      strcmp(lower, "cubierta") == 0 || strcmp(lower, "couverture") == 0 ||
      strcmp(lower, "capa") == 0 || strcmp(lower, "titelbild") == 0 ||
      strcmp(lower, "umschlag") == 0 || strcmp(lower, "copertina") == 0) {
    return true;
  }

  if (strncmp(lower, "cover", 5) == 0) {
    const char c = lower[5];
    if (c == '\0' || c == ' ' || c == ':' || c == '-' || c == '_') return true;
  }
  if (strncmp(lower, "front cover", 11) == 0) {
    const char c = lower[11];
    if (c == '\0' || c == ' ' || c == ':' || c == '-' || c == '_') return true;
  }
  return false;
}
}  // namespace

void TocNcxParser::startElement(void* userData, const char* name, const char** atts) {
  // NOTE: We rely on navPoint label and content coming before any nested navPoints, this will be fine:
  // <navPoint>
  //   <navLabel><text>Chapter 1</text></navLabel>
  //   <content src="ch1.html"/>
  //   <navPoint> ...nested... </navPoint>
  // </navPoint>
  //
  // This will NOT:
  // <navPoint>
  //   <navPoint> ...nested... </navPoint>
  //   <navLabel><text>Chapter 1</text></navLabel>
  //   <content src="ch1.html"/>
  // </navPoint>

  auto* self = static_cast<TocNcxParser*>(userData);

  if (self->state == START && strcmp(name, "ncx") == 0) {
    self->state = IN_NCX;
    return;
  }

  if (self->state == IN_NCX && strcmp(name, "navMap") == 0) {
    self->state = IN_NAV_MAP;
    return;
  }

  // <pageList> is a sibling of <navMap> and contains <pageTarget> elements that map
  // printed page numbers to spine locations (e.g. "OEBPS/c9_split_000.xhtml#page_3").
  if (self->state == IN_NCX && strcmp(name, "pageList") == 0) {
    self->state = IN_PAGE_LIST;
    return;
  }

  if (self->state == IN_PAGE_LIST && strcmp(name, "pageTarget") == 0) {
    self->state = IN_PAGE_TARGET;
    self->currentPageLabel.clear();
    self->currentPageSrc.clear();
    return;
  }

  if (self->state == IN_PAGE_TARGET && strcmp(name, "navLabel") == 0) {
    self->state = IN_PAGE_TARGET_LABEL;
    return;
  }

  if (self->state == IN_PAGE_TARGET_LABEL && strcmp(name, "text") == 0) {
    self->state = IN_PAGE_TARGET_LABEL_TEXT;
    return;
  }

  if (self->state == IN_PAGE_TARGET && strcmp(name, "content") == 0) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "src") == 0) {
        self->currentPageSrc = atts[i + 1];
        break;
      }
    }
    return;
  }

  // Handles both top-level and nested navPoints
  if ((self->state == IN_NAV_MAP || self->state == IN_NAV_POINT) && strcmp(name, "navPoint") == 0) {
    self->state = IN_NAV_POINT;
    self->currentDepth++;

    self->currentLabel.clear();
    self->currentSrc.clear();
    self->currentNavPointIsCover = false;
    for (int i = 0; atts[i]; i += 2) {
      if (strcasecmp(atts[i], "id") == 0 || strcasecmp(atts[i], "class") == 0) {
        if (strcasecmp(atts[i + 1], "cover") == 0 || strcasecmp(atts[i + 1], "cover-page") == 0 ||
            strcasecmp(atts[i + 1], "coverpage") == 0) {
          self->currentNavPointIsCover = true;
          break;
        }
      }
    }
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "navLabel") == 0) {
    self->state = IN_NAV_LABEL;
    return;
  }

  if (self->state == IN_NAV_LABEL && strcmp(name, "text") == 0) {
    self->state = IN_NAV_LABEL_TEXT;
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "content") == 0) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "src") == 0) {
        self->currentSrc = atts[i + 1];
        break;
      }
    }
    return;
  }
}

void TocNcxParser::characterData(void* userData, const char* s, const int len) {
  auto* self = static_cast<TocNcxParser*>(userData);
  if (self->state == IN_NAV_LABEL_TEXT) {
    self->currentLabel.append(s, len);
  } else if (self->state == IN_PAGE_TARGET_LABEL_TEXT) {
    self->currentPageLabel.append(s, len);
  }
}

void TocNcxParser::endElement(void* userData, const char* name) {
  auto* self = static_cast<TocNcxParser*>(userData);

  if (self->state == IN_NAV_LABEL_TEXT && strcmp(name, "text") == 0) {
    self->state = IN_NAV_LABEL;
    return;
  }

  if (self->state == IN_NAV_LABEL && strcmp(name, "navLabel") == 0) {
    self->state = IN_NAV_POINT;
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "navPoint") == 0) {
    self->currentDepth--;
    if (self->currentDepth == 0) {
      self->state = IN_NAV_MAP;
    }
    return;
  }

  if (self->state == IN_NAV_POINT && strcmp(name, "content") == 0) {
    // At this point (end of content tag), we likely have both Label (from previous tags) and Src.
    // This is the safest place to push the data, assuming <navLabel> always comes before <content>.
    // NCX spec says navLabel comes before content.
    if (!self->currentLabel.empty() && !self->currentSrc.empty()) {
      const std::string rawTarget = self->baseContentPath + self->currentSrc;
      const size_t pos = rawTarget.find('#');
      const std::string rawPath = pos == std::string::npos ? rawTarget : rawTarget.substr(0, pos);
      std::string href = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawPath));
      std::string anchor;

      if (pos != std::string::npos) {
        anchor = FsHelpers::decodeUriEscapes(rawTarget.substr(pos + 1));
      }

      if (self->coverHref.empty() && (self->currentNavPointIsCover || isCoverLabel(self->currentLabel))) {
        self->coverHref = href;
        LOG_DBG("TOC", "Found cover in NCX TOC: %s (label='%s')", self->coverHref.c_str(),
                self->currentLabel.c_str());
        if (self->stopOnCoverFound) {
          self->saxParser_.stop();
          return;
        }
      }

      if (self->cache) {
        self->cache->createTocEntry(self->currentLabel, href, anchor, self->currentDepth);
      }

      // Clear them so we don't re-add them if there are weird XML structures
      self->currentLabel.clear();
      self->currentSrc.clear();
    }
    return;
  }

  // <pageList> closing handlers
  if (self->state == IN_PAGE_TARGET_LABEL_TEXT && strcmp(name, "text") == 0) {
    self->state = IN_PAGE_TARGET_LABEL;
    return;
  }

  if (self->state == IN_PAGE_TARGET_LABEL && strcmp(name, "navLabel") == 0) {
    self->state = IN_PAGE_TARGET;
    return;
  }

  if (self->state == IN_PAGE_TARGET && strcmp(name, "pageTarget") == 0) {
    if (self->pageListSink && !self->currentPageLabel.empty() && !self->currentPageSrc.empty()) {
      const std::string rawTarget = self->baseContentPath + self->currentPageSrc;
      const size_t pos = rawTarget.find('#');
      const std::string rawPath = pos == std::string::npos ? rawTarget : rawTarget.substr(0, pos);
      std::string href = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawPath));
      std::string anchor;
      if (pos != std::string::npos) {
        anchor = FsHelpers::decodeUriEscapes(rawTarget.substr(pos + 1));
      }
      self->pageListSink->addEntry(href, anchor, self->currentPageLabel);
    }
    self->currentPageLabel.clear();
    self->currentPageSrc.clear();
    self->state = IN_PAGE_LIST;
    return;
  }

  if (self->state == IN_PAGE_LIST && strcmp(name, "pageList") == 0) {
    self->state = IN_NCX;
    return;
  }
}
