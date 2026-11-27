#include "OpdsProgressionSync.h"

#include <ArduinoJson.h>
#include <CrossPointRoots.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// Howard Hinnant's days-from-civil algorithm (public domain), avoiding mktime()'s local-timezone
// dependence and non-standard timegm().
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Parses an ISO 8601 string (e.g. "2026-09-01T12:00:00Z" or "2026-09-01T12:00:00.000Z") to time_t in UTC.
time_t parseIso8601(const std::string& iso) {
  if (iso.empty()) return 0;
  int y = 0, M = 0, d = 0, h = 0, m = 0, s = 0;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &M, &d, &h, &m, &s) >= 3) {
    if (M < 1 || M > 12 || d < 1 || d > 31) return 0;
    const int64_t days = daysFromCivil(y, static_cast<unsigned>(M), static_cast<unsigned>(d));
    return static_cast<time_t>(days * 86400LL + h * 3600LL + m * 60LL + s);
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

bool equalsIgnoreCase(const std::string& a, const char* b) {
  const size_t len = std::strlen(b);
  if (a.size() != len) return false;
  for (size_t i = 0; i < len; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Parses an HTTP-date (IMF-fixdate / RFC 1123, e.g. "Fri, 27 Nov 2026 08:46:38 GMT") to UTC time_t.
// Returns 0 if the string cannot be parsed.
time_t parseHttpDate(const std::string& value) {
  static constexpr const char* kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                            "jul", "aug", "sep", "oct", "nov", "dec"};
  char mon[4] = {};
  int d = 0, y = 0, h = 0, m = 0, s = 0;
  // Skip optional "<weekday>, " prefix if present.
  const char* p = value.c_str();
  if (const char* comma = std::strchr(p, ',')) {
    p = comma + 1;
  }
  if (sscanf(p, " %d %3[A-Za-z] %d %d:%d:%d", &d, mon, &y, &h, &m, &s) != 6) {
    return 0;
  }
  if (y < 1970 || d < 1 || d > 31 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 60) {
    return 0;
  }
  unsigned monthNum = 0;
  for (unsigned i = 0; i < 12; ++i) {
    if (equalsIgnoreCase(mon, kMonths[i])) {
      monthNum = i + 1;
      break;
    }
  }
  if (monthNum == 0) return 0;
  const int64_t days = daysFromCivil(y, monthNum, static_cast<unsigned>(d));
  return static_cast<time_t>(days * 86400LL + h * 3600LL + m * 60LL + s);
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

bool saveSyncConfig(const std::string& cachePath, const SyncConfig& config) {
  if (cachePath.empty() || config.progressionUrl.empty()) return false;

  Storage.mkdir(cachePath.c_str());

  JsonDocument doc;
  doc["progressionUrl"] = config.progressionUrl;
  doc["serverUrl"] = config.serverUrl;
  doc["lastSyncedModified"] = config.lastSyncedModified;
  doc["lastSyncedProgression"] = config.lastSyncedProgression;

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

bool saveSyncConfig(const std::string& cachePath, const std::string& progressionUrl, const std::string& serverUrl) {
  if (cachePath.empty() || progressionUrl.empty()) return false;

  SyncConfig config;
  loadSyncConfig(cachePath, config);
  config.progressionUrl = progressionUrl;
  if (!serverUrl.empty()) {
    config.serverUrl = serverUrl;
  }
  return saveSyncConfig(cachePath, config);
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

  time_t serverTime = 0;
  client.setResponseHeaderSink([&serverTime](const std::string& name, const std::string& value) {
    if (equalsIgnoreCase(name, "date")) {
      const time_t parsed = parseHttpDate(value);
      if (parsed > 0) {
        serverTime = parsed;
      }
    }
  });

  LOG_INF(TAG, "Fetching remote progression from %s", config.progressionUrl.c_str());
  const int getStatus = client.GET(config.progressionUrl);
  client.clearResponseHeaderSink();

  if (getStatus == 401 || getStatus == 403) {
    LOG_ERR(TAG, "Authentication failed (HTTP %d)", getStatus);
    return {SyncStatus::AUTH_ERROR, {}, "Authentication required"};
  }

  RemoteProgression remote;
  bool remoteValid = false;

  if (getStatus == 200) {
    if (!client.getBody().empty()) {
      const DeserializationError parseErr = parseProgressionBody(client.getBody(), remote);
      if (!parseErr) {
        remoteValid = true;
        LOG_INF(TAG, "Remote: progress=%.3f modified=%s title=%s ref=%s", remote.progression, remote.modified.c_str(),
                remote.title.c_str(), remote.reference.c_str());
      } else {
        LOG_ERR(TAG, "Failed to parse remote progression JSON: %s", parseErr.c_str());
      }
    }
  } else if (getStatus != 404 && getStatus != 204) {
    LOG_ERR(TAG, "Failed GET progression: HTTP %d", getStatus);
    return {SyncStatus::NETWORK_ERROR, {}, "HTTP error " + std::to_string(getStatus)};
  }

  const time_t remoteTime = remoteValid ? parseIso8601(remote.modified) : 0;
  const time_t lastLocalSyncedTime = parseIso8601(config.lastSyncedModified);

  const bool localMoved = std::fabs(localProgression - config.lastSyncedProgression) > 0.001f;
  const bool remoteMoved = remoteValid && remoteTime > lastLocalSyncedTime &&
                           std::fabs(remote.progression - config.lastSyncedProgression) > 0.001f;
  const bool differs = remoteValid && std::fabs(remote.progression - localProgression) > 0.001f;

  // Both sides have moved since they last agreed: the server holds a progression this device never
  // recorded, and this device holds one the server does not.
  const bool diverged = localMoved && remoteMoved && differs;

  // Which side wins, now that the exchange has the server's current state in hand.
  //
  // TAKE_REMOTE does not re-test `diverged`: the user answered against this same state, so
  // re-deriving whether it counts as a divergence could flip the answer on a clock or a rounding
  // edge. It only needs the two to disagree, which is what it is being asked to settle.
  if (resolution == ConflictResolution::TAKE_REMOTE && differs) {
    config.lastSyncedModified = remote.modified;
    config.lastSyncedProgression = remote.progression;
    saveSyncConfig(cachePath, config);
    LOG_INF(TAG, "Remote progress adopted (%.1f%%)", remote.progression * 100.0f);
    return {SyncStatus::SUCCESS_REMOTE_NEWER, remote, ""};
  }
  // ASK has no say in a divergence: it reports one and stops, leaving lastSynced* untouched so the
  // next exchange sees the same question rather than a half-resolved one.
  if (resolution == ConflictResolution::ASK) {
    if (diverged) {
      LOG_INF(TAG, "Diverged: device %.1f%% vs server %.1f%%; deferring to the user", localProgression * 100.0f,
              remote.progression * 100.0f);
      return {SyncStatus::CONFLICT, remote, ""};
    }
    if (!localMoved && remoteMoved && differs) {
      config.lastSyncedModified = remote.modified;
      config.lastSyncedProgression = remote.progression;
      saveSyncConfig(cachePath, config);
      LOG_INF(TAG, "Remote progress is newer (%.1f%%); adopting", remote.progression * 100.0f);
      return {SyncStatus::SUCCESS_REMOTE_NEWER, remote, ""};
    }
  }
  // ConflictResolution::TAKE_LOCAL -- and a TAKE_REMOTE whose state already matches the device --
  // fall through: in-sync below when they agree, the push otherwise.

  // If local and remote match closely, no update needed
  if (remoteValid && !differs) {
    config.lastSyncedModified = remote.modified;
    config.lastSyncedProgression = localProgression;
    saveSyncConfig(cachePath, config);
    return {SyncStatus::SUCCESS_IN_SYNC, remote, ""};
  }

  // Local is newer: push local progress to server via PUT.
  // Prefer the HTTP Date captured from GET so devices without an RTC/NTP clock still
  // send a wall-clock timestamp aligned with the server (preventing 409 date conflicts).
  const std::string nowIso = formatIso8601(serverTime);

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
    saveSyncConfig(cachePath, config);
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
      saveSyncConfig(cachePath, config);
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
