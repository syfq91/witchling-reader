#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

inline constexpr const char* kBookCacheStateFile = "opds_sync.json";

class FsFile {
 public:
  std::string path;
  std::string buffer;
  size_t pos = 0;
  bool writeMode = false;
  std::map<std::string, std::string>* store = nullptr;

  size_t size() const { return buffer.size(); }

  size_t write(const uint8_t* data, size_t len) {
    buffer.append(reinterpret_cast<const char*>(data), len);
    return len;
  }

  int read(char* dst, size_t len) {
    const size_t avail = pos < buffer.size() ? buffer.size() - pos : 0;
    const size_t n = len < avail ? len : avail;
    if (n > 0) {
      std::memcpy(dst, buffer.data() + pos, n);
      pos += n;
    }
    return static_cast<int>(n);
  }

  void close() {
    if (writeMode && store) {
      (*store)[path] = buffer;
    }
  }
};

class HalStorage {
 public:
  std::map<std::string, std::string> files;

  void clear() { files.clear(); }

  bool mkdir(const char*) { return true; }

  bool exists(const char* path) const { return files.find(path) != files.end(); }

  bool openFileForWrite(const char*, const std::string& path, FsFile& f) {
    f = FsFile{};
    f.path = path;
    f.writeMode = true;
    f.store = &files;
    return true;
  }

  bool openFileForRead(const char*, const std::string& path, FsFile& f) {
    auto it = files.find(path);
    if (it == files.end()) return false;
    f = FsFile{};
    f.path = path;
    f.buffer = it->second;
    f.writeMode = false;
    f.store = &files;
    return true;
  }
};

inline HalStorage Storage;
