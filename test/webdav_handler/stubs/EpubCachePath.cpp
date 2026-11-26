// Host-test stand-in for the Epub constructor/getter that BookCacheReset uses to name a book's cache.
// The real Epub.cpp drags the whole EPUB pipeline into this target; the cache folder name is only a hash.
#include <Epub.h>
#include <Xtc/XtcParser.h>

#include <functional>
#include <string>
#include <utility>

Epub::Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(this->filepath));
}

const std::string& Epub::getCachePath() const { return cachePath; }

// Xtc's inline constructor holds a unique_ptr<XtcParser>; the host target needs only its destructor.
xtc::XtcParser::~XtcParser() = default;
