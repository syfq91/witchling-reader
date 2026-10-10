#pragma once

#include <Print.h>
#include <common/FsApiConstants.h>  // for oflag_t
#include <freertos/semphr.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class HalFile;

// Name of the file at the root of a per-book cache directory that holds user state rather than
// derived cache: the OPDS Progression sync config, written once at download by
// src/network/OpdsProgressionSync. Sections, thumbnails and progress are rebuildable from the
// book; the progression endpoint is not -- nothing rewrites it but another download -- so every
// cache wipe passes it to removeDir()'s keepFile.
inline constexpr const char* kBookCacheStateFile = "opds_sync.json";

class HalStorage {
 public:
  HalStorage();
  bool begin();
  bool ready() const;
  // Unmount cleanly on the way into deep sleep: flushes the FAT/directory cache
  // and ends the card session. Call it AFTER the sleep path's last write.
  void prepareForSleep();

  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  // Low-memory helpers:
  // Stream the file contents to a `Print` (e.g. `Serial`, or any `Print`-derived object).
  // Returns true on success, false on failure.
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  // Read up to `bufferSize-1` bytes into `buffer`, null-terminating it. Returns bytes read.
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  // Read the whole file at `path` into `out`. Unlike readFile(), the size is
  // checked against `cap` before anything is allocated, so an unexpectedly
  // large file fails instead of claiming the heap. Fails on missing,
  // directory, empty, above-`cap` and short-read files.
  bool readFileToString(const char* moduleName, const std::string& path, size_t cap, std::string& out);
  // Write a string to `path` on the SD card. Overwrites existing file.
  // Returns true on success.
  bool writeFile(const char* path, const String& content);
  // Ensure a directory exists, creating it if necessary. Returns true on success.
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  // Opens an EXISTING file for reading and writing without truncating it, so a caller can seek
  // and rewrite part of it. openFileForWrite truncates, which is right for caches written in one
  // go and wrong for any store that grows in place (see FootnotePreviews, which appends note
  // text as the reader moves through a book). Fails when the file does not exist.
  bool openFileForUpdate(const char* moduleName, const char* path, HalFile& file);
  bool openFileForUpdate(const char* moduleName, const std::string& path, HalFile& file);
  // Deletes `path` and everything under it. With `keepFile`, a file of that name at the root of
  // `path` survives the wipe: the SDK takes `path` itself down with it, so the directory is then
  // recreated around the kept file. Fails if the wipe fails or that file cannot be put back.
  bool removeDir(const char* path, const char* keepFile = nullptr);
  bool copyFile(const char* moduleName, const std::string& srcPath, const char* dstPath);

  uint64_t sdTotalBytes() const;
  uint64_t sdUsedBytes();
  uint64_t sdFreeBytes();

  // Goes up whenever the firmware creates, removes, renames or opens for writing anything on the
  // card outside its own cache folder (/.crosspoint), and when a USB Drive session ends: whatever
  // was worked out from the card's contents before -- a folder's book count -- may no longer
  // hold. It errs towards going up: a write that changed nothing costs a recount, never a wrong
  // answer. Kept in RAM, so it means "since boot"; a card edited elsewhere while the device was
  // off or asleep (a wake is a boot) is not something it can see.
  uint32_t contentGeneration() const { return contentGeneration_.load(std::memory_order_relaxed); }

  // Whether the card may have changed since markContentSeen() -- across reboots too, which the
  // generation cannot say. The first change after the mark leaves a marker file in the firmware's
  // cache folder, and markContentSeen() removes it. A USB Drive session counts from its start: the
  // host may write anything, and the power may go before the session ends.
  bool contentChangedSinceMark();
  // Marks the card seen as it was at `generation` (a contentGeneration() read earlier): the marker
  // goes unless a change came since. Decided under the storage lock, so a change made by another
  // task while it runs is never lost with the marker.
  void markContentSeen(uint32_t generation);
  // A change the firmware found on the card rather than made -- a book it never wrote -- counted like
  // one of its own: the generation goes up and the marker is left.
  void noteFoundChange() { noteContentChange(nullptr); }

  static HalStorage& getInstance() { return instance; }

  class StorageLock;  // private class, used internally

 private:
  friend class HalFile;  // HalFile::rename() changes the card too

  void noteContentChange(const char* path);
  void persistChangeMarker();

  static HalStorage instance;

  bool initialized = false;
  SemaphoreHandle_t storageMutex = nullptr;
  std::atomic<uint32_t> contentGeneration_{0};
  int8_t changeMarker_ = -1;  // the marker file: -1 not looked for yet, 0 absent, 1 present
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;
  class Impl;
  std::unique_ptr<Impl> impl;
  explicit HalFile(std::unique_ptr<Impl> impl);

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  bool seek(size_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();  // read a single byte

  uint64_t size64();
  uint64_t fileSize64();
  bool seek64(uint64_t pos);
  bool seekSet64(uint64_t offset);
  uint64_t position64() const;
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b) override;
  bool rename(const char* newPath);
  bool getModifyDateTime(uint16_t* pdate, uint16_t* ptime);
  bool getCreateDateTime(uint16_t* pdate, uint16_t* ptime);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;
};

// Only do renaming FsFile to HalFile if this header is included by downstream code
// The renaming is to allow using the thread-safe HalFile instead of the raw FsFile, without needing to change the
// downstream code
#ifndef HAL_STORAGE_IMPL
using FsFile = HalFile;
#endif

// Downstream code must use Storage instead of SdMan
#ifdef SdMan
#undef SdMan
#endif
