#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <BoardConfig.h>
#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <HalCapabilities.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <cassert>
#include <cstring>
#include <ctime>
#include <new>
#include <optional>

#include "HalI2cBus.h"
#include "HalSpiBus.h"

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() {}

// True when the SD card really is on the SPI bus the display also uses. Native
// SDMMC boards (X4 Pro: 1-bit slot 1, CLK41/CMD42/DAT0 40) share nothing with
// the panel, so serializing their card against panel refreshes is pure
// contention for no safety benefit.
static bool sdSharesDisplaySpiBus() { return BoardConfig::ACTIVE.sdmmc.busWidth == 0; }

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() {
  // Create the mutex here rather than in the constructor: HalStorage::instance
  // is a global, and its constructor runs before the FreeRTOS scheduler starts.
  // Calling xSemaphoreCreateRecursiveMutex() that early corrupts the TLSF heap metadata.
  // Recursive ownership lets HalStorage replace an existing HalFile while it
  // already holds StorageLock; destroying the replaced SdFat handle may close it.
  if (!storageMutex) {
    storageMutex = xSemaphoreCreateRecursiveMutex();
    assert(storageMutex != nullptr);
  }
  // SD-over-SPI clock ceiling on the S3 boards.
  //
  // The SDK defaults to 40 MHz whenever a profile leaves sd.spiHz at 0. That is
  // proven on the C3, whose wiring and card socket we have years of field data
  // for, but nothing has validated it on an S3 board -- and 40 MHz is at the top
  // of what SD-over-SPI tolerates, so a marginal card or trace shows up as a
  // mount failure rather than as degraded throughput.
  //
  // Hold the S3 boards at 20 MHz until someone measures otherwise. This only
  // touches profiles that expressed no preference; a profile that sets spiHz
  // explicitly is left alone, so raising it later is a one-value profile change
  // rather than an edit here.
  //
  // Gated on the board actually having an SD SPI bus. The X4 Pro and X4 Classic
  // mount through the native SDMMC peripheral, where sd.spiHz is dead config:
  // setting it changed nothing and the log line announced a clock for a bus that
  // does not exist, which is exactly the sort of line that costs someone an hour
  // when they are reading a boot log looking for why storage is slow.
#if !FREEINK_MCU_C3
  if (HalCapabilities::sdUsesSpi() && BoardConfig::ACTIVE.sd.spiHz == 0) {
    BoardConfig::ACTIVE.sd.spiHz = 20000000;
    LOG_INF("SD", "SPI clock held at 20 MHz on this board (SDK default is 40 MHz, unvalidated here)");
  }
#endif

  {
    // SD init drives the shared bus, so it must be serialized against the
    // display too when this profile uses SPI. Native SDMMC is independent.
    const auto spiLock = sdSharesDisplaySpiBus() ? std::optional<HalSpiBus::Lock>(std::in_place) : std::nullopt;
    if (!SDCard.begin()) return false;
  }
  FsDateTime::setCallback([](uint16_t* date, uint16_t* time) {
    *date = FS_DATE(1980, 1, 1);
    *time = FS_TIME(0, 0, 0);
  });
  return true;
}

bool HalStorage::ready() const { return SDCard.ready(); }

void HalStorage::prepareForSleep() { SDCard.prepareForSleep(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock()
      // Conditional in the member-init list, not the body, so the SPI lock is
      // still acquired BEFORE storageMutex when it is taken at all — see the
      // ordering note below.
      : spiLock(sdSharesDisplaySpiBus() ? std::optional<HalSpiBus::Lock>(std::in_place) : std::nullopt) {
    xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY);
  }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }

 private:
  // Declared first so it is acquired before storageMutex and released after it:
  // the bus stays locked for the whole SD operation, and the lock order is
  // always SPI-outer/storage-inner, matching display code (which takes only the
  // SPI lock). Do not reorder this below any other member.
  //
  // Engaged only when the card is actually on that bus. On the C3 it always is,
  // so this is behaviour-identical there; on an SDMMC board it is disengaged and
  // SD I/O no longer waits behind a 1-2 s panel refresh. Upstream has no
  // equivalent coupling at all — HalSpiBus is fork-local, added because our
  // display and SD genuinely share one bus on the C3.
  std::optional<HalSpiBus::Lock> spiLock;
};

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

namespace {

// The firmware's own cache: written on every page turn, and never anything a folder count counts.
bool isCachePath(const char* path) {
  constexpr char kCacheRoot[] = "/.crosspoint";
  constexpr size_t kLength = sizeof(kCacheRoot) - 1;
  return strncmp(path, kCacheRoot, kLength) == 0 && (path[kLength] == '\0' || path[kLength] == '/');
}

bool opensForWriting(const oflag_t oflag) {
  return (oflag & O_ACCMODE) != O_RDONLY || (oflag & (O_CREAT | O_TRUNC | O_APPEND)) != 0;
}

// An open that may make its file or empty it: it changes the card before it returns.
bool changesOnOpen(const oflag_t oflag) { return (oflag & (O_CREAT | O_TRUNC)) != 0; }

}  // namespace

// `path` null: the change is somewhere unknown (a file renamed through its handle, a USB Drive
// session), which counts as a change.
void HalStorage::noteContentChange(const char* path) {
  if (path != nullptr && isCachePath(path)) return;
  contentGeneration_.fetch_add(1, std::memory_order_relaxed);
}

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

// Ported verbatim from crosspoint-reader PR #2734 by Justin Mitchell
// (@itsthisjustin).
//
// Composed of already-locked HalStorage/HalFile operations, so it takes no
// StorageLock of its own.
bool HalStorage::readFileToString(const char* moduleName, const std::string& path, size_t cap, std::string& out) {
  out.clear();
  HalFile file;
  if (!openFileForRead(moduleName, path, file)) return false;
  if (file.isDirectory()) return false;
  const size_t size = file.fileSize();
  if (size == 0 || size > cap) return false;
  out.resize(size);
  return file.read(out.data(), size) == static_cast<int>(size);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  noteContentChange(path);
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  noteContentChange(path);
  HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path);
}

uint64_t HalStorage::sdTotalBytes() const {
  StorageLock lock;
  return SDCard.sdTotalBytes();
}

uint64_t HalStorage::sdUsedBytes() {
  StorageLock lock;
  return SDCard.sdUsedBytes();
}

uint64_t HalStorage::sdFreeBytes() {
  uint64_t total = sdTotalBytes();
  uint64_t used = sdUsedBytes();
  if (total <= used) return 0;
  return total - used;
}

class HalFile::Impl {
 public:
  explicit Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  Impl(const char* path, const oflag_t oflag) : file(SDCard.open(path, oflag)) {}  // openInto()
  FsFile file;

  // The Impl for what an open returned, taken under the StorageLock. An open file gets one of its
  // own. A failed open gets `failed`, which costs no heap: a missing file or the end of a folder's
  // listing used to take and free an Impl each.
  //
  // When an open file's Impl cannot be had, the file is closed and the open fails like any other.
  // std::make_unique aborted the device there (-fno-exceptions); a handle without an Impl would be
  // one never opened, whose close() asserts, and callers may close any handle an open returned.
  static Impl* adopt(FsFile&& opened) {
    if (!opened.isOpen()) return &failed;
    if (auto* own = new (std::nothrow) Impl(std::move(opened))) return own;
    LOG_ERR("SD", "OOM: %u bytes for a file handle, the open fails", static_cast<unsigned>(sizeof(Impl)));
    opened.close();
    return &failed;
  }

  // An open that may make its file or empty it (changesOnOpen()): the Impl's block is taken BEFORE
  // the open, and the file opened straight into it. adopt() comes too late for one: the open has
  // made or emptied the file by then, so a refused Impl lost what an existing file held. Without
  // the block the open fails here with the card untouched; an open that fails gives the block back
  // and gets `failed`.
  static Impl* openInto(const char* path, const oflag_t oflag) {
    void* const block = ::operator new(sizeof(Impl), std::nothrow);
    if (!block) {
      LOG_ERR("SD", "OOM: %u bytes for a file handle, the open fails", static_cast<unsigned>(sizeof(Impl)));
      return &failed;
    }
    auto* const own = new (block) Impl(path, oflag);  // the file opened straight into its block
    if (own->file.isOpen()) return own;
    delete own;
    return &failed;
  }

  // A closed FsFile answers every call as a failed open does and changes nothing, so one serves
  // every failed open, from every task.
  static Impl failed;
};

HalFile::Impl HalFile::Impl::failed{FsFile()};

void HalFile::ImplDeleter::operator()(Impl* impl) const {
  if (impl != &Impl::failed) delete impl;
}

HalFile::HalFile() = default;

HalFile::HalFile(Impl* impl) : impl(impl) {}

HalFile::~HalFile() {
  if (!impl) return;
  HalStorage::StorageLock lock;
  impl.reset();
}

HalFile::HalFile(HalFile&&) = default;

HalFile& HalFile::operator=(HalFile&& other) {
  if (this == &other) return *this;
  HalStorage::StorageLock lock;
  impl = std::move(other.impl);
  return *this;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  if (opensForWriting(oflag)) noteContentChange(path);
  if (changesOnOpen(oflag)) return HalFile(HalFile::Impl::openInto(path, oflag));
  return HalFile(HalFile::Impl::adopt(SDCard.open(path, oflag)));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  noteContentChange(path);
  HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag);
}

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) {
  noteContentChange(path);
  HAL_STORAGE_WRAPPED_CALL(remove, path);
}
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  noteContentChange(oldPath);
  noteContentChange(newPath);
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) {
  noteContentChange(path);
  HAL_STORAGE_WRAPPED_CALL(rmdir, path);
}

// Ported from crosspoint-reader PR #3979 (phuclee, @phucisstupid). A directory entry is known by
// where it sits: the first cluster of its folder and its index in that folder. Every spelling that
// opens it, other letter case or 8.3 alias alike, lands on the same entry.
bool HalStorage::areSameFile(const char* first, const char* second, bool& same) {
  StorageLock lock;
  same = false;
  const auto locate = [](const char* path, uint32_t& folderCluster, uint32_t& entryIndex) {
    const std::string name(path);
    const size_t slash = name.rfind('/');
    const std::string folderPath = slash == std::string::npos || slash == 0 ? "/" : name.substr(0, slash);
    FsFile folder = SDCard.open(folderPath.c_str(), O_RDONLY);
    FsFile entry = SDCard.open(path, O_RDONLY);
    if (!folder || !folder.isDirectory() || !entry) {
      LOG_ERR("SD", "Cannot tell which file %s is", path);
      return false;
    }
    folderCluster = folder.firstCluster();
    entryIndex = entry.dirIndex();
    return true;
  };
  uint32_t firstFolder = 0, firstEntry = 0, secondFolder = 0, secondEntry = 0;
  if (!locate(first, firstFolder, firstEntry) || !locate(second, secondFolder, secondEntry)) return false;
  same = firstFolder == secondFolder && firstEntry == secondEntry;
  return true;
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  const bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(HalFile::Impl::adopt(std::move(fsFile)));
  return ok && file.isOpen();  // not open after all when its Impl could not be had
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  // With the flags SDCardManager::openFileForWrite opens it with, through open(): its Impl is had
  // before the file is emptied or made.
  file = open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) LOG_ERR(moduleName, "Failed to open %s for writing", path);
  return file.isOpen();
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForUpdate(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile = SDCard.open(path, O_RDWR);
  const bool ok = static_cast<bool>(fsFile);
  if (!ok) {
    LOG_ERR(moduleName, "Failed to open %s for update", path);
  }
  file = HalFile(HalFile::Impl::adopt(std::move(fsFile)));
  return ok && file.isOpen();
}

bool HalStorage::openFileForUpdate(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForUpdate(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path, const char* keepFile) {
  noteContentChange(path);
  if (keepFile == nullptr) {
    HAL_STORAGE_WRAPPED_CALL(removeDir, path);
  }

  StorageLock lock;
  const std::string dir(path);
  const std::string kept = dir + "/" + keepFile;
  const std::string stashed = dir + "." + keepFile + ".stashed";

  if (!SDCard.exists(kept.c_str())) {
    return SDCard.removeDir(dir.c_str());
  }

  // The SDK wipe takes the directory down with it, so the file to keep has to be outside while it
  // runs. A stashed copy left by an interrupted wipe is the older of the two: drop it.
  if (SDCard.exists(stashed.c_str())) SDCard.remove(stashed.c_str());
  if (!SDCard.rename(kept.c_str(), stashed.c_str())) {
    LOG_ERR("SD", "Could not set %s aside before wiping %s", kept.c_str(), dir.c_str());
    return false;
  }

  const bool wiped = SDCard.removeDir(dir.c_str());

  if (!SDCard.exists(dir.c_str()) && !SDCard.mkdir(dir.c_str(), true)) {
    LOG_ERR("SD", "Could not recreate %s; %s left at %s", dir.c_str(), keepFile, stashed.c_str());
    return false;
  }
  if (!SDCard.rename(stashed.c_str(), kept.c_str())) {
    LOG_ERR("SD", "Could not restore %s; left at %s", kept.c_str(), stashed.c_str());
    return false;
  }
  return wiped;
}

bool HalStorage::copyFile(const char* moduleName, const std::string& srcPath, const char* dstPath) {
  HalFile src, dst;
  if (!openFileForRead(moduleName, srcPath, src)) return false;
  if (!openFileForWrite(moduleName, dstPath, dst)) {
    src.close();
    return false;
  }
  constexpr size_t BUF_SIZE = 4096;
  auto* buf = new (std::nothrow) uint8_t[BUF_SIZE];
  if (!buf) {
    dst.close();
    src.close();
    remove(dstPath);
    return false;
  }
  bool ok = true;
  while (src.available()) {
    const auto bytesRead = src.read(buf, BUF_SIZE);
    if (bytesRead <= 0) {
      // available() said there was more, so this is a failed read, not the end of the file.
      ok = false;
      break;
    }
    if (dst.write(buf, bytesRead) != static_cast<size_t>(bytesRead)) {
      ok = false;
      break;
    }
  }
  delete[] buf;
  dst.close();
  src.close();
  // Callers treat an existing destination as a finished copy (cover and thumb caches do), so a
  // failed or partial one must not stay behind.
  if (!ok) remove(dstPath);
  return ok;
}

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() {
  assert(impl != nullptr);
  return static_cast<size_t>(impl->file.size());
}
size_t HalFile::fileSize() {
  assert(impl != nullptr);
  return static_cast<size_t>(impl->file.fileSize());
}
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const {
  assert(impl != nullptr);
  return static_cast<size_t>(impl->file.position());
}
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) {
  Storage.noteContentChange(nullptr);  // the old path is not known here
  HAL_FILE_WRAPPED_CALL(rename, newPath);
}
bool HalFile::getModifyDateTime(uint16_t* pdate, uint16_t* ptime) {
  HAL_FILE_WRAPPED_CALL(getModifyDateTime, pdate, ptime);
}
bool HalFile::getCreateDateTime(uint16_t* pdate, uint16_t* ptime) {
  HAL_FILE_WRAPPED_CALL(getCreateDateTime, pdate, ptime);
}
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
uint64_t HalFile::size64() { HAL_FILE_FORWARD_CALL(size, ); }
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekSet64(uint64_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
uint64_t HalFile::position64() const { HAL_FILE_FORWARD_CALL(position, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  // A falsy handle ends a listing, so one whose Impl could not be had ends it early: logged by
  // adopt(), and no worse than the abort it replaces.
  return HalFile(Impl::adopt(impl->file.openNextFile()));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
