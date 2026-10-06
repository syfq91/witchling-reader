#include "OpdsProgressionSync.h"

#include <ArduinoJson.h>
#include <CrossPointRoots.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "SystemStatus.h"

namespace OpdsProgressionSync {

namespace {

constexpr const char* TAG = "OPDS_SYNC";

// The filename is the HAL's kBookCacheStateFile: every cache wipe keeps that name at the root of
// the cache dir (HalStorage::removeDir's keepFile), so the config outlives a wipe of its own dir.
std::string syncFilePath(const std::string& cachePath) { return cachePath + "/" + kBookCacheStateFile; }

// Parses an ISO 8601 string (e.g. "2026-09-01T12:00:00Z" or "2026-09-01T12:00:00.000Z") to time_t in UTC.
time_t parseIso8601(const std::string& iso) {
  if (iso.empty()) return 0;
  int y = 0, M = 0, d = 0, h = 0, m = 0, s = 0;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &M, &d, &h, &m, &s) >= 3) {
    struct tm tmUtc{};
    tmUtc.tm_year = y - 1900;
    tmUtc.tm_mon = M - 1;
    tmUtc.tm_mday = d;
    tmUtc.tm_hour = h;
    tmUtc.tm_min = m;
    tmUtc.tm_sec = s;
    return mktime(&tmUtc);
  }
  return 0;
}

std::string formatIso8601(time_t t) {
  if (t == 0) t = time(nullptr);
  struct tm tmUtc{};
  gmtime_r(&t, &tmUtc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
  return std::string(buf);
}

// Parses an OPDS Progression document body into `out`. Returns a non-Ok
// ArduinoJson error when the body is not valid JSON.
DeserializationError parseProgressionBody(const std::string& body, RemoteProgression& out) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) return err;
  out.progression = doc["progression"] | 0.0f;
  out.modified = doc["modified"] | "";
  out.title = doc["title"] | "";
  if (doc["references"].is<JsonArray>() && doc["references"].as<JsonArray>().size() > 0) {
    out.reference = doc["references"][0].as<std::string>();
  }
  if (doc["device"].is<JsonObject>()) {
    out.deviceId = doc["device"]["id"] | "";
    out.deviceName = doc["device"]["name"] | "";
  }
  return DeserializationError::Ok;
}

const OpdsServer* findServerCredentials(const std::string& serverUrl, const std::string& progressionUrl) {
  const auto& servers = OPDS_STORE.getServers();
  for (const auto& server : servers) {
    if (!serverUrl.empty() && server.url == serverUrl) {
      return &server;
    }
  }
  for (const auto& server : servers) {
    if (!server.url.empty() && progressionUrl.rfind(server.url, 0) == 0) {
      return &server;
    }
  }
  return nullptr;
}

}  // namespace

std::string computeCachePath(const std::string& filePath, const std::string& cacheBase) {
  const size_t hash = std::hash<std::string>{}(filePath);
  if (FsHelpers::checkFileExtension(filePath, ".xtc")) {
    return cacheBase + "/xtc_" + std::to_string(hash);
  }
  return cacheBase + "/epub_" + std::to_string(hash);
}

bool hasSyncConfig(const std::string& cachePath) {
  if (cachePath.empty()) return false;
  return Storage.exists(syncFilePath(cachePath).c_str());
}

bool saveSyncConfig(const std::string& cachePath, const std::string& progressionUrl, const std::string& serverUrl) {
  if (cachePath.empty() || progressionUrl.empty()) return false;

  Storage.mkdir(cachePath.c_str());

  SyncConfig existing;
  loadSyncConfig(cachePath, existing);

  JsonDocument doc;
  doc["progressionUrl"] = progressionUrl;
  doc["serverUrl"] = serverUrl.empty() ? existing.serverUrl : serverUrl;
  doc["lastSyncedModified"] = existing.lastSyncedModified;
  doc["lastSyncedProgression"] = existing.lastSyncedProgression;

  std::string jsonStr;
  serializeJson(doc, jsonStr);

  FsFile f;
  const std::string configPath = syncFilePath(cachePath);
  if (!Storage.openFileForWrite(TAG, configPath, f)) {
    LOG_ERR(TAG, "Failed to open %s for writing", configPath.c_str());
    return false;
  }
  f.write(reinterpret_cast<const uint8_t*>(jsonStr.data()), jsonStr.size());
  f.close();
  LOG_DBG(TAG, "Saved sync config to %s", configPath.c_str());
  return true;
}

bool loadSyncConfig(const std::string& cachePath, SyncConfig& config) {
  if (cachePath.empty()) return false;

  FsFile f;
  if (!Storage.openFileForRead(TAG, syncFilePath(cachePath), f)) {
    return false;
  }

  const size_t size = f.size();
  if (size == 0 || size > 4096) {
    f.close();
    return false;
  }

  std::string jsonStr(size, '\0');
  if (f.read(jsonStr.data(), size) != static_cast<int>(size)) {
    f.close();
    return false;
  }
  f.close();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonStr);
  if (err) {
    LOG_ERR(TAG, "Failed to parse sync config JSON: %s", err.c_str());
    return false;
  }

  config.progressionUrl = doc["progressionUrl"] | "";
  config.serverUrl = doc["serverUrl"] | "";
  config.lastSyncedModified = doc["lastSyncedModified"] | "";
  config.lastSyncedProgression = doc["lastSyncedProgression"] | 0.0f;

  return !config.progressionUrl.empty();
}

SyncResult performSync(const std::string& cachePath, float localProgression, const std::string& localTitle,
                       const std::string& localReference, const ConflictResolution resolution) {
  SyncConfig config;
  if (!loadSyncConfig(cachePath, config)) {
    return {SyncStatus::NO_CONFIG, {}, "No sync configuration found"};
  }

  if (WiFi.status() != WL_CONNECTED) {
    return {SyncStatus::NO_WIFI, {}, "WiFi not connected"};
  }

  crosspoint::SecureHttpClient client;
  client.setTimeout(15000);
  client.setCACert(CROSSPOINT_ROOTS_PEM);
  client.setAllowCertificateDateErrors(true);
  client.setAllowInsecureFallback(SETTINGS.skipHttpsValidation != 0);

  const OpdsServer* creds = findServerCredentials(config.serverUrl, config.progressionUrl);
  if (creds && !creds->username.empty()) {
    client.setBasicAuth(creds->username, creds->password);
  }

  client.addHeader("Accept", "application/opds-progression+json");

  LOG_INF(TAG, "Fetching remote progression from %s", config.progressionUrl.c_str());
  const int getStatus = client.GET(config.progressionUrl);

  if (getStatus == 401 || getStatus == 403) {
    LOG_ERR(TAG, "Authentication failed (HTTP %d)", getStatus);
    return {SyncStatus::AUTH_ERROR, {}, "Authentication required"};
  }

  RemoteProgression remote;
  bool remoteValid = false;

  if (getStatus == 200) {
    const DeserializationError parseErr = parseProgressionBody(client.getBody(), remote);
    if (!parseErr) {
      remoteValid = true;
      LOG_INF(TAG, "Remote: progress=%.3f modified=%s title=%s ref=%s", remote.progression, remote.modified.c_str(),
              remote.title.c_str(), remote.reference.c_str());
    } else {
      LOG_ERR(TAG, "Failed to parse remote progression JSON: %s", parseErr.c_str());
    }
  } else if (getStatus != 404 && getStatus != 204) {
    LOG_ERR(TAG, "Failed GET progression: HTTP %d", getStatus);
    return {SyncStatus::NETWORK_ERROR, {}, "HTTP error " + std::to_string(getStatus)};
  }

  const time_t remoteTime = remoteValid ? parseIso8601(remote.modified) : 0;
  const time_t lastLocalSyncedTime = parseIso8601(config.lastSyncedModified);

  // Both sides have moved since they last agreed: the server holds a progression this device never
  // recorded, and this device holds one the server does not.
  const bool diverged = remoteValid && remoteTime > lastLocalSyncedTime &&
                        std::fabs(remote.progression - config.lastSyncedProgression) > 0.001f &&
                        std::fabs(remote.progression - localProgression) > 0.001f;

  // Which side wins, now that the exchange has the server's current state in hand.
  //
  // TAKE_REMOTE does not re-test `diverged`: the user answered against this same state, so
  // re-deriving whether it counts as a divergence could flip the answer on a clock or a rounding
  // edge. It only needs the two to disagree, which is what it is being asked to settle.
  if (resolution == ConflictResolution::TAKE_REMOTE && remoteValid &&
      std::fabs(remote.progression - localProgression) > 0.001f) {
    config.lastSyncedModified = remote.modified;
    config.lastSyncedProgression = remote.progression;
    saveSyncConfig(cachePath, config.progressionUrl, config.serverUrl);
    LOG_INF(TAG, "Remote progress adopted (%.1f%%)", remote.progression * 100.0f);
    return {SyncStatus::SUCCESS_REMOTE_NEWER, remote, ""};
  }
  // ASK has no say in a divergence: it reports one and stops, leaving lastSynced* untouched so the
  // next exchange sees the same question rather than a half-resolved one.
  if (resolution == ConflictResolution::ASK && diverged) {
    LOG_INF(TAG, "Diverged: device %.1f%% vs server %.1f%%; deferring to the user", localProgression * 100.0f,
            remote.progression * 100.0f);
    return {SyncStatus::CONFLICT, remote, ""};
  }
  // ConflictResolution::TAKE_LOCAL -- and a TAKE_REMOTE whose state already matches the device --
  // fall through: in-sync below when they agree, the push otherwise.

  // If local and remote match closely, no update needed
  if (remoteValid && std::fabs(remote.progression - localProgression) <= 0.001f) {
    config.lastSyncedModified = remote.modified;
    config.lastSyncedProgression = localProgression;
    saveSyncConfig(cachePath, config.progressionUrl, config.serverUrl);
    return {SyncStatus::SUCCESS_IN_SYNC, remote, ""};
  }

  // Local is newer: push local progress to server via PUT
  const std::string nowIso = formatIso8601(0);

  JsonDocument putDoc;
  putDoc["progression"] = localProgression;
  putDoc["modified"] = nowIso;
  if (!localTitle.empty()) {
    putDoc["title"] = localTitle;
  }
  if (!localReference.empty()) {
    JsonArray refs = putDoc["references"].to<JsonArray>();
    refs.add(localReference);
  }

  JsonObject dev = putDoc["device"].to<JsonObject>();
  dev["id"] = "witchreader-" + WiFi.macAddress();
  dev["name"] = "Witch(hunt) Reader X4";

  std::string putBody;
  serializeJson(putDoc, putBody);

  client.clearHeaders();
  client.addHeader("Content-Type", "application/opds-progression+json");
  client.addHeader("Accept", "application/opds-progression+json");
  if (creds && !creds->username.empty()) {
    client.setBasicAuth(creds->username, creds->password);
  }

  LOG_INF(TAG, "Pushing local progress (%.1f%%) to %s", localProgression * 100.0f, config.progressionUrl.c_str());
  const int putStatus = client.PUT(config.progressionUrl, putBody);

  if (putStatus == 200 || putStatus == 201 || putStatus == 204) {
    // 201 is the spec answer for a progression row this PUT created (200 updates
    // an existing one); both mean the push landed.
    config.lastSyncedModified = nowIso;
    config.lastSyncedProgression = localProgression;
    saveSyncConfig(cachePath, config.progressionUrl, config.serverUrl);
    LOG_INF(TAG, "Progress pushed successfully");
    return {SyncStatus::SUCCESS_LOCAL_PUSHED, {}, ""};
  }

  if (putStatus == 401 || putStatus == 403) {
    LOG_ERR(TAG, "Authentication failed pushing progress (HTTP %d)", putStatus);
    return {SyncStatus::AUTH_ERROR, {}, "Authentication required"};
  }

  if (putStatus == 409) {
    // Stale precondition: another device stored a newer progression while this one was pushing.
    // Re-fetch it. Adopting it outright would silently override the position the user just chose,
    // so it goes back to them as a CONFLICT unless the two turn out to agree already.
    const int regetStatus = client.GET(config.progressionUrl);
    if (regetStatus == 401 || regetStatus == 403) {
      LOG_ERR(TAG, "Authentication failed re-fetching progression (HTTP %d)", regetStatus);
      return {SyncStatus::AUTH_ERROR, {}, "Authentication required"};
    }
    RemoteProgression remoteNow;
    if (regetStatus == 200 && !parseProgressionBody(client.getBody(), remoteNow)) {
      if (std::fabs(remoteNow.progression - localProgression) > 0.001f) {
        LOG_INF(TAG, "Push rejected as stale; server holds %.1f%%, asking again", remoteNow.progression * 100.0f);
        return {SyncStatus::CONFLICT, remoteNow, ""};
      }
      config.lastSyncedModified = remoteNow.modified;
      config.lastSyncedProgression = remoteNow.progression;
      saveSyncConfig(cachePath, config.progressionUrl, config.serverUrl);
      LOG_INF(TAG, "Push rejected as stale, but the server already holds the same position");
      return {SyncStatus::SUCCESS_IN_SYNC, remoteNow, ""};
    }
    LOG_ERR(TAG, "Re-fetch after progression conflict failed: HTTP %d", regetStatus);
    return {SyncStatus::NETWORK_ERROR, {}, "Failed to update progression (HTTP 409)"};
  }

  LOG_ERR(TAG, "Failed to push progress: HTTP %d", putStatus);
  return {SyncStatus::NETWORK_ERROR, {}, "Failed to update progression (HTTP " + std::to_string(putStatus) + ")"};
}

}  // namespace OpdsProgressionSync
