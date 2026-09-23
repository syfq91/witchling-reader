#include "TocNavParser.h"

#include <FsHelpers.h>
#include <Logging.h>

#include <algorithm>

#include "../BookMetadataCache.h"
#include "PageListSink.h"

bool TocNavParser::setup() {
  // The EPUB3 nav doc is XHTML: enable bare-void-tag repair (its <head> may
  // carry HTML-style unclosed <meta>/<link> from sloppy converters).
  if (!saxParser_.init(this, startElement, endElement, characterData, nullptr, /*htmlVoidTagRepair=*/true)) {
    LOG_DBG("NAV", "Couldn't allocate memory for parser");
    return false;
  }
  return true;
}

TocNavParser::~TocNavParser() = default;

size_t TocNavParser::write(const uint8_t data) { return write(&data, 1); }

size_t TocNavParser::write(const uint8_t* buffer, const size_t size) {
  if (!saxParser_.isActive()) return 0;

  remainingSize -= std::min(size, remainingSize);
  if (!saxParser_.feed(buffer, size)) {
    LOG_DBG("NAV", "Parse error at line %d: %s", saxParser_.errorLine(), saxParser_.errorString());
    return 0;
  }
  if (remainingSize == 0) {
    if (!saxParser_.finalize()) {
      LOG_DBG("NAV", "Parse error (finalize): %s", saxParser_.errorString());
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

void TocNavParser::startElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<TocNavParser*>(userData);

  // Track HTML structure loosely - we mainly care about finding <nav epub:type="toc">
  if (strcmp(name, "html") == 0) {
    self->state = IN_HTML;
    return;
  }

  if (self->state == IN_HTML && strcmp(name, "body") == 0) {
    self->state = IN_BODY;
    return;
  }

  // Look for <nav epub:type="toc">, <nav epub:type="page-list">, or <nav epub:type="landmarks"> anywhere in body.
  // Both navs are siblings under <body>; we don't expect them to nest.
  if (self->state >= IN_BODY && self->state != IN_NAV_TOC && self->state != IN_NAV_PAGE_LIST &&
      self->state != IN_NAV_LANDMARKS && strcmp(name, "nav") == 0) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "epub:type") == 0 || strcmp(atts[i], "type") == 0 || strcmp(atts[i], "role") == 0) {
        if (strcmp(atts[i + 1], "toc") == 0 || strcmp(atts[i + 1], "doc-toc") == 0) {
          self->state = IN_NAV_TOC;
          LOG_DBG("NAV", "Found nav toc element");
          return;
        }
        if (strcmp(atts[i + 1], "page-list") == 0 || strcmp(atts[i + 1], "doc-pagelist") == 0) {
          self->state = IN_NAV_PAGE_LIST;
          LOG_DBG("NAV", "Found nav page-list element");
          return;
        }
        if (strcmp(atts[i + 1], "landmarks") == 0 || strcmp(atts[i + 1], "doc-landmarks") == 0) {
          self->state = IN_NAV_LANDMARKS;
          LOG_DBG("NAV", "Found nav landmarks element");
          return;
        }
      }
    }
    return;
  }

  // Page-list nav: parallel state machine (independent ol/li/a tracking).
  if (self->state >= IN_NAV_PAGE_LIST && self->state <= IN_PL_ANCHOR) {
    if (strcmp(name, "ol") == 0) {
      self->plOlDepth++;
      self->state = IN_PL_OL;
      return;
    }
    if (self->state == IN_PL_OL && strcmp(name, "li") == 0) {
      self->state = IN_PL_LI;
      self->currentPageLabel.clear();
      self->currentPageHref.clear();
      return;
    }
    if (self->state == IN_PL_LI && strcmp(name, "a") == 0) {
      self->state = IN_PL_ANCHOR;
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "href") == 0) {
          self->currentPageHref = atts[i + 1];
          break;
        }
      }
      return;
    }
    return;
  }

  // Landmarks nav: inspect links for epub:type="cover"
  if (self->state >= IN_NAV_LANDMARKS && self->state <= IN_LM_ANCHOR) {
    if (strcmp(name, "a") == 0) {
      self->state = IN_LM_ANCHOR;
      const char* hrefAtt = nullptr;
      bool isCover = false;
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "href") == 0) {
          hrefAtt = atts[i + 1];
        } else if (strcmp(atts[i], "epub:type") == 0 || strcmp(atts[i], "type") == 0) {
          if (strcmp(atts[i + 1], "cover") == 0) {
            isCover = true;
          }
        }
      }
      if (isCover && hrefAtt != nullptr && self->coverHref.empty()) {
        const std::string rawTarget = self->baseContentPath + hrefAtt;
        const size_t pos = rawTarget.find('#');
        const std::string rawPath = pos == std::string::npos ? rawTarget : rawTarget.substr(0, pos);
        self->coverHref = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawPath));
        LOG_DBG("NAV", "Found cover in landmarks: %s", self->coverHref.c_str());
        if (self->stopOnCoverFound) {
          self->saxParser_.stop();
          return;
        }
      }
      return;
    }
    return;
  }

  // Only process ol/li/a if we're inside the toc nav
  if (self->state < IN_NAV_TOC) {
    return;
  }

  if (strcmp(name, "ol") == 0) {
    self->olDepth++;
    self->state = IN_OL;
    return;
  }

  if (self->state == IN_OL && strcmp(name, "li") == 0) {
    self->state = IN_LI;
    self->currentLabel.clear();
    self->currentHref.clear();
    return;
  }

  if (self->state == IN_LI && strcmp(name, "a") == 0) {
    self->state = IN_ANCHOR;
    self->currentAnchorIsCover = false;
    // Get href attribute and check for epub:type="cover"
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "href") == 0) {
        self->currentHref = atts[i + 1];
      } else if (strcmp(atts[i], "epub:type") == 0 || strcmp(atts[i], "type") == 0) {
        if (strcmp(atts[i + 1], "cover") == 0) {
          self->currentAnchorIsCover = true;
        }
      }
    }
    return;
  }
}

void TocNavParser::characterData(void* userData, const char* s, const int len) {
  auto* self = static_cast<TocNavParser*>(userData);

  // Collect text inside the anchor of either nav (TOC or page-list).
  if (self->state == IN_ANCHOR) {
    self->currentLabel.append(s, len);
  } else if (self->state == IN_PL_ANCHOR) {
    self->currentPageLabel.append(s, len);
  }
}

void TocNavParser::endElement(void* userData, const char* name) {
  auto* self = static_cast<TocNavParser*>(userData);

  // ---- Page-list nav close handlers (checked before TOC handlers because IN_PL_* states
  // sort after IN_NAV_TOC, but we want exact-state matching either way).
  if (strcmp(name, "a") == 0 && self->state == IN_PL_ANCHOR) {
    if (self->pageListSink && !self->currentPageLabel.empty() && !self->currentPageHref.empty()) {
      const std::string rawTarget = self->baseContentPath + self->currentPageHref;
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
    self->currentPageHref.clear();
    self->state = IN_PL_LI;
    return;
  }

  if (strcmp(name, "li") == 0 && (self->state == IN_PL_LI || self->state == IN_PL_OL)) {
    self->state = IN_PL_OL;
    return;
  }

  if (strcmp(name, "ol") == 0 &&
      (self->state == IN_PL_OL || self->state == IN_PL_LI || self->state == IN_NAV_PAGE_LIST)) {
    if (self->plOlDepth > 0) {
      self->plOlDepth--;
    }
    self->state = (self->plOlDepth == 0) ? IN_NAV_PAGE_LIST : IN_PL_LI;
    return;
  }

  if (strcmp(name, "nav") == 0 &&
      (self->state == IN_NAV_PAGE_LIST || self->state == IN_PL_OL || self->state == IN_PL_LI)) {
    self->state = IN_BODY;
    self->plOlDepth = 0;
    LOG_DBG("NAV", "Finished parsing nav page-list");
    return;
  }

  // ---- Landmarks nav close handlers
  if (strcmp(name, "a") == 0 && self->state == IN_LM_ANCHOR) {
    self->state = IN_NAV_LANDMARKS;
    return;
  }

  if (strcmp(name, "nav") == 0 && (self->state == IN_NAV_LANDMARKS || self->state == IN_LM_ANCHOR)) {
    self->state = IN_BODY;
    LOG_DBG("NAV", "Finished parsing nav landmarks");
    return;
  }

  // ---- TOC nav close handlers
  if (strcmp(name, "a") == 0 && self->state == IN_ANCHOR) {
    // Create TOC entry when closing anchor tag (we have all data now)
    if (!self->currentLabel.empty() && !self->currentHref.empty()) {
      const std::string rawTarget = self->baseContentPath + self->currentHref;
      const size_t pos = rawTarget.find('#');
      const std::string rawPath = pos == std::string::npos ? rawTarget : rawTarget.substr(0, pos);
      std::string href = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawPath));
      std::string anchor;

      if (pos != std::string::npos) {
        anchor = FsHelpers::decodeUriEscapes(rawTarget.substr(pos + 1));
      }

      if (self->coverHref.empty() && (self->currentAnchorIsCover || isCoverLabel(self->currentLabel))) {
        self->coverHref = href;
        LOG_DBG("NAV", "Found cover in TOC: %s (label='%s')", self->coverHref.c_str(),
                self->currentLabel.c_str());
        if (self->stopOnCoverFound) {
          self->saxParser_.stop();
          return;
        }
      }

      if (self->cache) {
        // olDepth gives us the nesting level (1-based from the outer ol)
        self->cache->createTocEntry(self->currentLabel, href, anchor, self->olDepth);
      }

      self->currentLabel.clear();
      self->currentHref.clear();
    }
    self->state = IN_LI;
    return;
  }

  if (strcmp(name, "li") == 0 && (self->state == IN_LI || self->state == IN_OL)) {
    self->state = IN_OL;
    return;
  }

  if (strcmp(name, "ol") == 0 && (self->state == IN_OL || self->state == IN_LI || self->state == IN_NAV_TOC)) {
    if (self->olDepth > 0) {
      self->olDepth--;
    }
    self->state = (self->olDepth == 0) ? IN_NAV_TOC : IN_LI;
    return;
  }

  if (strcmp(name, "nav") == 0 && (self->state == IN_NAV_TOC || self->state == IN_OL || self->state == IN_LI)) {
    self->state = IN_BODY;
    self->olDepth = 0;
    LOG_DBG("NAV", "Finished parsing nav toc");
    return;
  }
}
