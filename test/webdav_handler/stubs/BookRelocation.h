#pragma once

#include <string>

// Host-test stand-in: only the cache-path helper the WebDAV tests use.
namespace BookRelocation {

std::string cacheDirFor(const std::string& path);
void moved(const std::string& oldPath, const std::string& newPath);

}  // namespace BookRelocation
