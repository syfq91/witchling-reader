#pragma once

#include <cstdint>
#include <string>

namespace OpdsProgressionSync {

struct SyncConfig {
  std::string progressionUrl;
  std::string serverUrl;
  std::string lastSyncedModified;
  float lastSyncedProgression = 0.0f;
};

struct RemoteProgression {
  float progression = 0.0f;
  std::string modified;
  std::string title;
  std::string reference;
  std::string deviceId;
  std::string deviceName;
};

enum class SyncStatus {
  SUCCESS_REMOTE_NEWER,  // Remote progress is newer; caller should jump
  SUCCESS_LOCAL_PUSHED,  // Local progress was pushed to server
  SUCCESS_IN_SYNC,       // Local and remote already match
  CONFLICT,              // Both sides moved; caller must ask the user which one to keep
  NO_CONFIG,             // No sync configuration for this book
  NO_WIFI,               // WiFi is not connected
  NETWORK_ERROR,         // HTTP / connection error
  AUTH_ERROR,            // HTTP 401 / 403 unauthorized
  PARSE_ERROR            // Failed to parse response JSON
};

// What performSync does when the device and the server have BOTH moved since they last
// agreed (the CONFLICT condition). ASK is the default so no caller can silently pick a
// winner: the exchange reports the divergence and the caller puts it to the user.
enum class ConflictResolution {
  ASK,         // return SyncStatus::CONFLICT with the remote state, config untouched
  TAKE_LOCAL,  // the user chose this device: push the local position
  TAKE_REMOTE  // the user chose the server: adopt the remote position
};

struct SyncResult {
  SyncStatus status = SyncStatus::NO_CONFIG;
  RemoteProgression remote;
  std::string errorMessage;
};

// Check if a book's cache directory contains sync configuration.
bool hasSyncConfig(const std::string& cachePath);

// Save or update OPDS progression sync configuration for a book.
bool saveSyncConfig(const std::string& cachePath, const std::string& progressionUrl, const std::string& serverUrl);

// Load sync configuration from disk.
bool loadSyncConfig(const std::string& cachePath, SyncConfig& config);

// Compute book cache path for a file path (e.g. /.crosspoint/epub_<hash>).
std::string computeCachePath(const std::string& filePath, const std::string& cacheBase = "/.crosspoint");

// Perform the sync exchange with the OPDS Progression 1.0 endpoint.
// localProgression: 0.0f - 1.0f
// localTitle: chapter or book title
// localReference: spine item href or anchor
// resolution: how to settle a divergence; ASK reports SyncStatus::CONFLICT and leaves the
//             stored lastSynced* untouched so the caller can put it to the user.
SyncResult performSync(const std::string& cachePath, float localProgression, const std::string& localTitle = "",
                       const std::string& localReference = "", ConflictResolution resolution = ConflictResolution::ASK);

}  // namespace OpdsProgressionSync
