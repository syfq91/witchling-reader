// Host coverage for HalStorage::removeDir's keepFile argument.
//
// keepFile is how the OPDS Progression config (kBookCacheStateFile) survives a
// wipe of the cache directory it lives in: the config names an endpoint nothing
// on the device can derive from the book again, so a cache clear -- which drops
// everything derived from the book -- must not drop it too. The device
// implementation cannot run here (it goes through SDCardManager), so this
// exercises the std::filesystem twin in test/zip_entry_reader/HalStorage.h,
// which mirrors it line for line, including the stash-around-the-wipe dance.
//
// What the stash is for: the SDK's removeDir takes the directory itself down
// with it, so the file to keep has to be outside while the wipe runs. That makes
// an interrupted wipe the one window where a copy exists at neither path, and a
// copy left at the stashed path the one where it may be stale -- both are pinned
// below.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "HalStorage.h"

namespace fs = std::filesystem;

namespace {

class RemoveDirKeepFile : public ::testing::Test {
 protected:
  // Every case owns a directory named after the case: gtest_discover_tests runs
  // each TEST as its own process and ctest runs them side by side, so no two
  // may share a path.
  void SetUp() override {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir = fs::temp_directory_path() / ("hal_storage_" + std::string(info->name()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  static void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
  }

  static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  fs::path dir;
};

}  // namespace

// The case every caller depends on: the state file's bytes are untouched and
// everything derived from the book -- flat files and subdirectories alike --
// is gone.
TEST_F(RemoveDirKeepFile, KeepsStateFileAndDropsEverythingElse) {
  const fs::path state = dir / kBookCacheStateFile;
  writeFile(state, "{\"progressionUrl\":\"https://example/1\"}");
  writeFile(dir / "book.bin", "derived");
  fs::create_directories(dir / "spines");
  writeFile(dir / "spines" / "0.bin", "derived");

  ASSERT_TRUE(Storage.removeDir(dir.c_str(), kBookCacheStateFile));

  EXPECT_TRUE(fs::exists(state));
  EXPECT_EQ(readFile(state), "{\"progressionUrl\":\"https://example/1\"}");
  EXPECT_FALSE(fs::exists(dir / "book.bin"));
  EXPECT_FALSE(fs::exists(dir / "spines"));
}

// No state file in the directory: the wipe is a plain one and takes the
// directory with it, the way it did before keepFile existed.
TEST_F(RemoveDirKeepFile, WipesTheDirectoryWhenThereIsNothingToKeep) {
  writeFile(dir / "book.bin", "derived");

  ASSERT_TRUE(Storage.removeDir(dir.c_str(), kBookCacheStateFile));

  EXPECT_FALSE(fs::exists(dir));
}

// keepFile absent -- every caller that is not clearing a book cache (fonts,
// spine caches, firmware self-cache) still asks for the old behaviour.
TEST_F(RemoveDirKeepFile, WithoutKeepFileRemovesTheDirectory) {
  writeFile(dir / kBookCacheStateFile, "{}");
  writeFile(dir / "book.bin", "derived");

  ASSERT_TRUE(Storage.removeDir(dir.c_str()));

  EXPECT_FALSE(fs::exists(dir));
}

// The stash lives beside the directory ("<dir>.<keepFile>.stashed"), so an
// interrupted wipe can leave one behind. It is the older copy -- the live one
// was already moved out when the wipe started -- so it must be dropped rather
// than restored over the state file.
TEST_F(RemoveDirKeepFile, ReplacesAStashLeftByAnInterruptedWipe) {
  writeFile(dir / kBookCacheStateFile, "live");
  writeFile(dir / "book.bin", "derived");
  const fs::path stash = fs::path(dir.string() + "." + kBookCacheStateFile + ".stashed");
  writeFile(stash, "stale");

  ASSERT_TRUE(Storage.removeDir(dir.c_str(), kBookCacheStateFile));

  EXPECT_EQ(readFile(dir / kBookCacheStateFile), "live");
  EXPECT_FALSE(fs::exists(stash));
  EXPECT_FALSE(fs::exists(dir / "book.bin"));
}
