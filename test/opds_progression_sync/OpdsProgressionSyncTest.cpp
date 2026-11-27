#include <ArduinoJson.h>
#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <gtest/gtest.h>

#include <cstdlib>

#include "CrossPointSettings.h"
#include "OpdsProgressionSync.h"
#include "OpdsServerStore.h"

namespace {

constexpr const char* kCachePath = "/.crosspoint/epub_test";
constexpr const char* kServerUrl = "https://jellyfin.example.com/opds";
constexpr const char* kProgUrl = "https://jellyfin.example.com/opds/progression/book-1";

class OpdsProgressionSyncTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.clear();
    crosspoint::gMockHttp.reset();
    WiFi.currentStatus = WL_CONNECTED;
    SETTINGS.skipHttpsValidation = 0;
    OPDS_STORE.servers = {{kServerUrl, "reader", "secret"}};
  }
};

TEST_F(OpdsProgressionSyncTest, SaveSyncConfigPersistsLastSyncedState) {
  OpdsProgressionSync::SyncConfig cfg;
  cfg.progressionUrl = kProgUrl;
  cfg.serverUrl = kServerUrl;
  cfg.lastSyncedModified = "2026-10-01T12:00:00Z";
  cfg.lastSyncedProgression = 0.42f;

  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, cfg));

  OpdsProgressionSync::SyncConfig loaded;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, loaded));
  EXPECT_EQ(loaded.progressionUrl, kProgUrl);
  EXPECT_EQ(loaded.serverUrl, kServerUrl);
  EXPECT_EQ(loaded.lastSyncedModified, "2026-10-01T12:00:00Z");
  EXPECT_NEAR(loaded.lastSyncedProgression, 0.42f, 1e-5f);

  // Updating URLs preserves previously persisted lastSynced* fields.
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, kProgUrl, kServerUrl));
  OpdsProgressionSync::SyncConfig reloaded;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, reloaded));
  EXPECT_EQ(reloaded.lastSyncedModified, "2026-10-01T12:00:00Z");
  EXPECT_NEAR(reloaded.lastSyncedProgression, 0.42f, 1e-5f);
}

TEST_F(OpdsProgressionSyncTest, Empty200GetPushesLocalAndPersistsState) {
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, kProgUrl, kServerUrl));

  // Jellyfin returns 200 OK with empty body when no progression is stored yet.
  crosspoint::gMockHttp.responses.push_back({200, ""});
  crosspoint::gMockHttp.responses.push_back({200, R"({"progression":0.25,"modified":"2026-11-25T04:00:00Z"})"});

  const auto res = OpdsProgressionSync::performSync(kCachePath, 0.25f, "Chapter 2", "Text/ch02.xhtml");
  EXPECT_EQ(res.status, OpdsProgressionSync::SyncStatus::SUCCESS_LOCAL_PUSHED);

  ASSERT_EQ(crosspoint::gMockHttp.requests.size(), 2u);
  EXPECT_EQ(crosspoint::gMockHttp.requests[0].method, "GET");
  EXPECT_EQ(crosspoint::gMockHttp.requests[0].basicUser, "reader");
  EXPECT_EQ(crosspoint::gMockHttp.requests[0].headers["Accept"], "application/opds-progression+json");

  EXPECT_EQ(crosspoint::gMockHttp.requests[1].method, "PUT");
  EXPECT_EQ(crosspoint::gMockHttp.requests[1].headers["Content-Type"], "application/opds-progression+json");
  EXPECT_EQ(crosspoint::gMockHttp.requests[1].basicUser, "reader");

  JsonDocument putDoc;
  ASSERT_FALSE(deserializeJson(putDoc, crosspoint::gMockHttp.requests[1].body));
  EXPECT_NEAR(putDoc["progression"].as<float>(), 0.25f, 1e-5f);
  EXPECT_EQ(putDoc["title"].as<std::string>(), "Chapter 2");
  EXPECT_EQ(putDoc["references"][0].as<std::string>(), "Text/ch02.xhtml");
  EXPECT_FALSE(putDoc["device"]["id"].as<std::string>().empty());
  EXPECT_FALSE(putDoc["device"]["name"].as<std::string>().empty());

  OpdsProgressionSync::SyncConfig loaded;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, loaded));
  EXPECT_FALSE(loaded.lastSyncedModified.empty());
  EXPECT_NEAR(loaded.lastSyncedProgression, 0.25f, 1e-5f);
}

TEST_F(OpdsProgressionSyncTest, RemoteOnlyAdvancementAdoptsRemoteWithoutConflict) {
  OpdsProgressionSync::SyncConfig cfg;
  cfg.progressionUrl = kProgUrl;
  cfg.serverUrl = kServerUrl;
  cfg.lastSyncedModified = "2026-10-01T12:00:00Z";
  cfg.lastSyncedProgression = 0.25f;
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, cfg));

  // Local stayed at 0.25, remote moved to 0.60 at a newer timestamp.
  crosspoint::gMockHttp.responses.push_back(
      {200,
       R"({"progression":0.60,"modified":"2026-10-02T12:00:00Z","title":"Ch 6","references":["Text/ch06.xhtml"],"device":{"id":"web","name":"Jellyfin"}})"});

  const auto res = OpdsProgressionSync::performSync(kCachePath, 0.25f, "Ch 2", "Text/ch02.xhtml");
  EXPECT_EQ(res.status, OpdsProgressionSync::SyncStatus::SUCCESS_REMOTE_NEWER);
  EXPECT_NEAR(res.remote.progression, 0.60f, 1e-5f);
  EXPECT_EQ(res.remote.reference, "Text/ch06.xhtml");

  OpdsProgressionSync::SyncConfig loaded;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, loaded));
  EXPECT_EQ(loaded.lastSyncedModified, "2026-10-02T12:00:00Z");
  EXPECT_NEAR(loaded.lastSyncedProgression, 0.60f, 1e-5f);
}

TEST_F(OpdsProgressionSyncTest, BothSidesMovedReportsConflictAndRespectsResolution) {
  OpdsProgressionSync::SyncConfig cfg;
  cfg.progressionUrl = kProgUrl;
  cfg.serverUrl = kServerUrl;
  cfg.lastSyncedModified = "2026-10-01T12:00:00Z";
  cfg.lastSyncedProgression = 0.25f;
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, cfg));

  // Local moved to 0.40, remote moved to 0.60.
  const std::string remoteBody =
      R"({"progression":0.60,"modified":"2026-10-02T12:00:00Z","device":{"id":"web","name":"Jellyfin"}})";
  crosspoint::gMockHttp.responses.push_back({200, remoteBody});

  const auto askRes = OpdsProgressionSync::performSync(kCachePath, 0.40f, "", "", OpdsProgressionSync::ConflictResolution::ASK);
  EXPECT_EQ(askRes.status, OpdsProgressionSync::SyncStatus::CONFLICT);

  // ASK must leave lastSynced* untouched.
  OpdsProgressionSync::SyncConfig afterAsk;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, afterAsk));
  EXPECT_EQ(afterAsk.lastSyncedModified, "2026-10-01T12:00:00Z");
  EXPECT_NEAR(afterAsk.lastSyncedProgression, 0.25f, 1e-5f);

  // TAKE_REMOTE adopts remote and persists it.
  crosspoint::gMockHttp.responses.push_back({200, remoteBody});
  const auto takeRemote =
      OpdsProgressionSync::performSync(kCachePath, 0.40f, "", "", OpdsProgressionSync::ConflictResolution::TAKE_REMOTE);
  EXPECT_EQ(takeRemote.status, OpdsProgressionSync::SyncStatus::SUCCESS_REMOTE_NEWER);

  OpdsProgressionSync::SyncConfig afterRemote;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, afterRemote));
  EXPECT_EQ(afterRemote.lastSyncedModified, "2026-10-02T12:00:00Z");
  EXPECT_NEAR(afterRemote.lastSyncedProgression, 0.60f, 1e-5f);
}

TEST_F(OpdsProgressionSyncTest, ParseIso8601IsUtcIndependentOfLocalTimezone) {
  // Set a non-UTC timezone (UTC+9) to ensure parseIso8601 does not shift by local TZ.
  const char* prevTz = std::getenv("TZ");
  const std::string savedTz = prevTz ? prevTz : "";
  setenv("TZ", "JST-9", 1);
  tzset();

  OpdsProgressionSync::SyncConfig cfg;
  cfg.progressionUrl = kProgUrl;
  cfg.serverUrl = kServerUrl;
  cfg.lastSyncedModified = "2026-10-01T12:00:00Z";
  cfg.lastSyncedProgression = 0.25f;
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, cfg));

  // Remote timestamp is 1 hour AFTER lastSyncedModified in UTC, which would look older if
  // parsed as JST-9 vs UTC.
  crosspoint::gMockHttp.responses.push_back(
      {200, R"({"progression":0.50,"modified":"2026-10-01T13:00:00Z","device":{"id":"web","name":"Jellyfin"}})"});

  const auto res = OpdsProgressionSync::performSync(kCachePath, 0.25f);
  EXPECT_EQ(res.status, OpdsProgressionSync::SyncStatus::SUCCESS_REMOTE_NEWER);

  if (prevTz) {
    setenv("TZ", savedTz.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
}

TEST_F(OpdsProgressionSyncTest, UsesHttpDateHeaderFromGetForPutModified) {
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, kProgUrl, kServerUrl));

  crosspoint::gMockHttp.responses.push_back({200, "", {{"Date", "Fri, 27 Nov 2026 08:46:38 GMT"}}});
  crosspoint::gMockHttp.responses.push_back({200, R"({"progression":0.30,"modified":"2026-11-27T08:46:38Z"})"});

  const auto res = OpdsProgressionSync::performSync(kCachePath, 0.30f, "Chapter 3", "Text/ch03.xhtml");
  EXPECT_EQ(res.status, OpdsProgressionSync::SyncStatus::SUCCESS_LOCAL_PUSHED);
  ASSERT_EQ(crosspoint::gMockHttp.requests.size(), 2u);

  JsonDocument putDoc;
  ASSERT_FALSE(deserializeJson(putDoc, crosspoint::gMockHttp.requests[1].body));
  EXPECT_EQ(putDoc["modified"].as<std::string>(), "2026-11-27T08:46:38Z");

  OpdsProgressionSync::SyncConfig loaded;
  ASSERT_TRUE(OpdsProgressionSync::loadSyncConfig(kCachePath, loaded));
  EXPECT_EQ(loaded.lastSyncedModified, "2026-11-27T08:46:38Z");
}

TEST_F(OpdsProgressionSyncTest, IgnoresMalformedHttpDateHeaderAndFallsBackToSystemClock) {
  ASSERT_TRUE(OpdsProgressionSync::saveSyncConfig(kCachePath, kProgUrl, kServerUrl));

  crosspoint::gMockHttp.responses.push_back({200, "", {{"date", "not-a-valid-date"}}});
  crosspoint::gMockHttp.responses.push_back({200, R"({"progression":0.30,"modified":"2026-11-27T08:46:38Z"})"});

  const auto res = OpdsProgressionSync::performSync(kCachePath, 0.30f);
  EXPECT_EQ(res.status, OpdsProgressionSync::SyncStatus::SUCCESS_LOCAL_PUSHED);
  ASSERT_EQ(crosspoint::gMockHttp.requests.size(), 2u);

  JsonDocument putDoc;
  ASSERT_FALSE(deserializeJson(putDoc, crosspoint::gMockHttp.requests[1].body));
  EXPECT_FALSE(putDoc["modified"].as<std::string>().empty());
  EXPECT_NE(putDoc["modified"].as<std::string>(), "not-a-valid-date");
}

}  // namespace
