#include "WifiSelectionActivity.h"

#include <GfxRenderer.h>
#include <HTTPClient.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <NetworkClient.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

#include <atomic>
#include <cstring>
#include <ctime>
#include <map>

#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/ConfirmDialog.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {

std::atomic<uint32_t> wifiEventGeneration{0};
std::atomic<unsigned long> wifiEventConnectionStartMs{0};
std::atomic<bool> wifiEventAttemptAssociated{false};

void readDeviceBaseMac(uint8_t mac[6]) { esp_efuse_mac_get_default(mac); }

std::string formatMacLabel(const uint8_t mac[6]) {
  char macStr[64];
  snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  return std::string(macStr);
}

std::string formatMacDashed(const uint8_t mac[6]) {
  char persistedMac[18];
  snprintf(persistedMac, sizeof(persistedMac), "%02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  return std::string(persistedMac);
}

String formatMacCompact(const uint8_t mac[6]) {
  char compactMac[13];
  snprintf(compactMac, sizeof(compactMac), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(compactMac);
}

}  // namespace

void WifiSelectionActivity::onEnter() {
  Activity::onEnter();
  const uint32_t eventGeneration = wifiEventGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

  // Timing instrumentation: split total connect time into association vs DHCP.
  // STA_CONNECTED = association (auth + 4-way handshake done).
  // STA_GOT_IP    = DHCP done.
  evtIdConnected = WiFi.onEvent(
      [eventGeneration](WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
        if (wifiEventGeneration.load(std::memory_order_acquire) != eventGeneration) return;
        wifiEventAttemptAssociated.store(true, std::memory_order_relaxed);
        // Which AP we landed on, not just when. On a mesh SSID the sorted candidate list is the
        // whole story: pairing this BSSID with the one in the disconnect below says whether a
        // failed attempt cost us a bad node, or whether the same node needed two tries.
        LOG_DBG("WIFI", "EVT associated at %lu ms: bssid=%s ch=%u",
                millis() - wifiEventConnectionStartMs.load(std::memory_order_relaxed),
                formatMacDashed(info.wifi_sta_connected.bssid).c_str(), info.wifi_sta_connected.channel);
      },
      ARDUINO_EVENT_WIFI_STA_CONNECTED);
  evtIdGotIp = WiFi.onEvent(
      [eventGeneration](WiFiEvent_t /*event*/, WiFiEventInfo_t /*info*/) {
        if (wifiEventGeneration.load(std::memory_order_acquire) != eventGeneration) return;
        LOG_DBG("WIFI", "EVT got_ip at %lu ms", millis() - wifiEventConnectionStartMs.load(std::memory_order_relaxed));
      },
      ARDUINO_EVENT_WIFI_STA_GOT_IP);
  // STA_START = the driver finished esp_wifi_start() (PHY init + RF calibration). Splits
  // driver bring-up from the scan/auth/assoc that follows it, which the association timestamp
  // alone cannot: measured begin->associated is a flat ~2.5 s regardless of scan method, hint,
  // or RSSI, and that invariance is what this pair exists to explain.
  evtIdStaStart = WiFi.onEvent(
      [eventGeneration](WiFiEvent_t /*event*/, WiFiEventInfo_t /*info*/) {
        if (wifiEventGeneration.load(std::memory_order_acquire) != eventGeneration) return;
        LOG_DBG("WIFI", "EVT sta_start at %lu ms (driver up; scan/auth begins here)",
                millis() - wifiEventConnectionStartMs.load(std::memory_order_relaxed));
      },
      ARDUINO_EVENT_WIFI_STA_START);
  // The one that should settle it. A cost that flat across every configuration looks like a
  // fixed retry/backoff rather than a negotiation, and a retry means a disconnect event with a
  // reason code. If nothing fires between begin() and STA_CONNECTED, the ~2.5 s is genuinely
  // the AP taking that long and there is nothing here to win; if AUTH_EXPIRE / ASSOC_EXPIRE /
  // HANDSHAKE_TIMEOUT shows up mid-connect, that names the second we are paying for.
  evtIdDisconnected = WiFi.onEvent(
      [eventGeneration](WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
        if (wifiEventGeneration.load(std::memory_order_acquire) != eventGeneration) return;
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        // bssid/rssi name the AP that failed and how loud it was at the moment it gave up, which
        // is what separates "the driver picked a node it cannot actually hold" from "the whole
        // SSID is too weak here".
        LOG_DBG("WIFI", "EVT disconnected at %lu ms: reason=%u (%s) assoc=%d bssid=%s rssi=%d",
                millis() - wifiEventConnectionStartMs.load(std::memory_order_relaxed), reason,
                WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)),
                wifiEventAttemptAssociated.load(std::memory_order_relaxed) ? 1 : 0,
                formatMacDashed(info.wifi_sta_disconnected.bssid).c_str(),
                static_cast<int>(info.wifi_sta_disconnected.rssi));
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Load saved WiFi credentials - SD card operations need lock as we use SPI
  // for both
  {
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
  }

  // Use base MAC from eFuse (stable per-device, independent of WiFi init timing).
  uint8_t mac[6];
  readDeviceBaseMac(mac);
  cachedMacAddress = formatMacLabel(mac);

  // Reset state
  selectedNetworkIndex = 0;
  networks.clear();
  state = WifiSelectionState::SCANNING;
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  usedSavedPassword = false;
  savePromptSelection = 0;
  forgetPromptSelection = 0;
  autoConnecting = false;
  autoCycleCandidates.clear();
  autoCycleCandidateIndex = 0;
  autoCycleAfterScan = false;

  const std::string persistedMac = formatMacDashed(mac);
  if (WIFI_STORE.getLastKnownMacAddress() != persistedMac) {
    RenderLock lock(*this);
    WIFI_STORE.setLastKnownMacAddress(persistedMac);
  }

  // Free the large blocks BEFORE the radio comes up, for every caller at once.
  //
  // This is the one place every WiFi session passes through, and the callers had drifted: five
  // trimmed before launching this activity, while the web server, Calibre, Weather and OPDS
  // trimmed only after a successful join — which is too late to help the join itself. Bringing
  // up the stack is already allocation-heavy (RX/TX buffers, the WPA supplicant, RF calibration
  // data), so on a lean heap the association is exactly what fails, and it surfaces as a plain
  // connect timeout with no association event rather than as an obvious OOM.
  //
  // Doing it here costs the callers that already trim nothing: the helper is idempotent, and
  // every one of them releases the secondary buffer during its session anyway, so this only
  // moves the release earlier. The primary buffer stays, so this activity keeps rendering.
  trimMemoryForNetworkSession(renderer, "WIFI");

  resetUi();
  app.setScreen(screenTrampoline, this);
  app.on(ACTION_ROW, actionTrampoline, this);
  app.on(ACTION_BACK, actionTrampoline, this);
  app.on(ACTION_RESCAN, actionTrampoline, this);
  app.on(ACTION_OPTIONS, actionTrampoline, this);
  app.on(ACTION_PROMPT_YES, actionTrampoline, this);
  app.on(ACTION_PROMPT_NO, actionTrampoline, this);
  app.on(ACTION_FORGET_CANCEL, actionTrampoline, this);
  app.on(ACTION_FORGET_RESET, actionTrampoline, this);
  app.on(ACTION_FORGET_CONFIRM, actionTrampoline, this);
  app.on(ACTION_FAILED_DONE, actionTrampoline, this);
  app.on(ACTION_CAPTIVE_DONE, actionTrampoline, this);

  // Trigger first update to show scanning message
  requestUpdate();

  // Attempt to auto-connect to the last network
  if (allowAutoConnect) {
    const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
    if (!lastSsid.empty()) {
      const auto* cred = WIFI_STORE.findCredential(lastSsid);
      if (cred) {
        LOG_DBG("WIFI", "Attempting to auto-connect to %s", lastSsid.c_str());
        selectedSSID = cred->ssid;
        enteredPassword = cred->password;
        selectedRequiresPassword = !cred->password.empty();
        usedSavedPassword = true;
        autoConnecting = true;
        attemptConnection();
        requestUpdate();
        return;
      }
    }
  }

  // Fallback to scanning
  startWifiScan();
}

void WifiSelectionActivity::onExit() {
  resetUi();
  wifiEventGeneration.fetch_add(1, std::memory_order_acq_rel);
  Activity::onExit();

  if (evtIdConnected != 0) {
    WiFi.removeEvent(evtIdConnected);
    evtIdConnected = 0;
  }
  if (evtIdGotIp != 0) {
    WiFi.removeEvent(evtIdGotIp);
    evtIdGotIp = 0;
  }
  if (evtIdStaStart != 0) {
    WiFi.removeEvent(evtIdStaStart);
    evtIdStaStart = 0;
  }
  if (evtIdDisconnected != 0) {
    WiFi.removeEvent(evtIdDisconnected);
    evtIdDisconnected = 0;
  }

  LOG_DBG("WIFI", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  // Stop any ongoing WiFi scan
  LOG_DBG("WIFI", "Deleting WiFi scan...");
  WiFi.scanDelete();
  LOG_DBG("WIFI", "Free heap after scanDelete: %d bytes", ESP.getFreeHeap());

  // Note: We do NOT disconnect WiFi here - the parent activity
  // (CrossPointWebServerActivity) manages WiFi connection state. We just clean
  // up the scan and task.

  LOG_DBG("WIFI", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void WifiSelectionActivity::startWifiScan() {
  autoConnecting = false;
  // autoCycleAfterScan intentionally preserved when set by the auto-cycle flow
  state = WifiSelectionState::SCANNING;
  networks.clear();
  requestUpdate();

  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Start async scan
  WiFi.scanNetworks(true);  // true = async scan
}

void WifiSelectionActivity::buildAutoCycleCandidates() {
  autoCycleCandidates.clear();
  autoCycleCandidateIndex = 0;

  const std::string& skipSsid = WIFI_STORE.getLastConnectedSsid();  // already tried

  struct Candidate {
    std::string ssid;
    int32_t rssi;
  };
  std::vector<Candidate> candidates;

  for (const auto& net : networks) {
    if (net.ssid == skipSsid) continue;
    if (!WIFI_STORE.hasSavedCredential(net.ssid)) continue;
    candidates.push_back({net.ssid, net.rssi});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.rssi > b.rssi; });

  std::transform(candidates.begin(), candidates.end(), std::back_inserter(autoCycleCandidates),
                 [](const Candidate& c) { return c.ssid; });

  LOG_DBG("WIFI", "Auto-cycle candidates: %zu", autoCycleCandidates.size());
}

void WifiSelectionActivity::tryNextAutoCycleCandidate() {
  if (autoCycleCandidateIndex >= autoCycleCandidates.size()) {
    // All candidates exhausted — fall through to manual selection
    LOG_DBG("WIFI", "Auto-cycle exhausted, falling through to network list");
    state = WifiSelectionState::NETWORK_LIST;
    selectedNetworkIndex = 0;
    requestUpdate();
    return;
  }

  const std::string& ssid = autoCycleCandidates[autoCycleCandidateIndex++];
  const auto* cred = WIFI_STORE.findCredential(ssid);
  if (!cred) {
    tryNextAutoCycleCandidate();  // Credential disappeared, skip
    return;
  }

  LOG_DBG("WIFI", "Auto-cycle trying %s (%zu/%zu)", ssid.c_str(), autoCycleCandidateIndex, autoCycleCandidates.size());

  selectedSSID = cred->ssid;
  enteredPassword = cred->password;
  selectedRequiresPassword = !cred->password.empty();
  usedSavedPassword = true;
  autoConnecting = false;

  state = WifiSelectionState::AUTO_CYCLING;
  connectionStartTime = millis();
  wifiEventConnectionStartMs.store(connectionStartTime, std::memory_order_relaxed);
  connectedIP.clear();
  connectionError.clear();
  requestUpdate();

  prepareForConnect();
  issueWifiBegin();
}

void WifiSelectionActivity::processWifiScanResults() {
  const int16_t scanResult = WiFi.scanComplete();

  if (scanResult == WIFI_SCAN_RUNNING) {
    // Scan still in progress
    return;
  }

  if (scanResult == WIFI_SCAN_FAILED) {
    autoCycleAfterScan = false;
    state = WifiSelectionState::NETWORK_LIST;
    requestUpdate();
    return;
  }

  // Scan complete, process results
  // Use a map to deduplicate networks by SSID, keeping the strongest signal
  std::map<std::string, WifiNetworkInfo> uniqueNetworks;

  for (int i = 0; i < scanResult; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    const int32_t rssi = WiFi.RSSI(i);

    // Skip hidden networks (empty SSID)
    if (ssid.empty()) {
      continue;
    }

    // Check if we've already seen this SSID
    auto it = uniqueNetworks.find(ssid);
    if (it == uniqueNetworks.end() || rssi > it->second.rssi) {
      // New network or stronger signal than existing entry
      WifiNetworkInfo network;
      network.ssid = ssid;
      network.rssi = rssi;
      network.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
      uniqueNetworks[ssid] = network;
    }
  }

  // Convert map to vector
  networks.clear();
  for (const auto& pair : uniqueNetworks) {
    // cppcheck-suppress useStlAlgorithm
    networks.push_back(pair.second);
  }

  // Sort: saved-password networks first, then by signal strength (strongest first)
  std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
    if (a.hasSavedPassword != b.hasSavedPassword) {
      return a.hasSavedPassword;
    }
    return a.rssi > b.rssi;
  });

  WiFi.scanDelete();

  if (autoCycleAfterScan) {
    autoCycleAfterScan = false;
    buildAutoCycleCandidates();
    tryNextAutoCycleCandidate();
    return;
  }

  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  requestUpdate();
}

void WifiSelectionActivity::selectNetwork(const int index) {
  if (index < 0 || index >= static_cast<int>(networks.size())) {
    return;
  }

  const auto& network = networks[index];
  selectedSSID = network.ssid;
  selectedRequiresPassword = network.isEncrypted;
  usedSavedPassword = false;
  enteredPassword.clear();
  autoConnecting = false;

  // Check if we have saved credentials for this network
  const auto* savedCred = WIFI_STORE.findCredential(selectedSSID);
  if (savedCred && !savedCred->password.empty()) {
    // Use saved password - connect directly
    enteredPassword = savedCred->password;
    usedSavedPassword = true;
    LOG_DBG("WiFi", "Using saved password for %s, length: %zu", selectedSSID.c_str(), enteredPassword.size());
    attemptConnection();
    return;
  }

  if (selectedRequiresPassword) {
    // Show password entry
    state = WifiSelectionState::PASSWORD_ENTRY;
    // Don't allow screen updates while changing activity
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD),
                                                                   "",  // No initial text
                                                                   64,  // Max password length
                                                                   InputType::Password),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               state = WifiSelectionState::NETWORK_LIST;
                             } else {
                               enteredPassword = std::get<KeyboardResult>(result.data).text;
                               // state will be updated in next loop iteration
                             }
                           });
  } else {
    // Connect directly for open networks
    attemptConnection();
  }
}

void WifiSelectionActivity::attemptConnection() {
  state = autoConnecting ? WifiSelectionState::AUTO_CONNECTING : WifiSelectionState::CONNECTING;
  connectionStartTime = millis();
  wifiEventConnectionStartMs.store(connectionStartTime, std::memory_order_relaxed);
  connectedIP.clear();
  connectionError.clear();
  requestUpdate();

  prepareForConnect();
  issueWifiBegin();
}

void WifiSelectionActivity::prepareForConnect() {
  // Before anything touches the radio. WiFi does not work below 80 MHz on this SoC and the idle
  // governor parks the CPU at 10 MHz, so association from there hangs rather than failing — the
  // observed symptom was a dead device after a long-press into KOReader sync, whose hold crossed
  // the idle threshold before the action fired. main.cpp now keeps the clock up while a button is
  // held, which removes that particular trigger; this stays as the guarantee at the point that
  // actually depends on it, for every other way the clock could be low when we get here.
  powerManager.ensureFullSpeedForRadio();

  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect

  // Only switch mode if we're not already STA — the mode setter touches the netif and
  // can take 50+ ms even when "no change" semantically.
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
  }

  // Only do the heavy disconnect(true,true) — which erases NVS and tears down the WPA
  // state machine — when there's actually something to tear down. From a fresh/idle
  // state it's a pure cost (~50–80 ms on this SoC).
  const wl_status_t status = WiFi.status();
  const bool needsReset = (status == WL_CONNECTED) || (status == WL_CONNECT_FAILED) || (status == WL_CONNECTION_LOST) ||
                          (status == WL_NO_SSID_AVAIL);
  if (needsReset) {
    WiFi.disconnect(true, true);
  }

  // Modem sleep OFF for the whole network session. The Arduino core arms WIFI_PS_MIN_MODEM at
  // STA_START, so it is active across the scan/auth/assoc we are about to pay for: the radio
  // dozes between DTIM beacons, and a missed beacon on a weak link is reported as
  // reason=200 (BEACON_TIMEOUT) with assoc=0 -- a ~10 s dead attempt before the driver retries.
  // OtaUpdater already does this for its transfer (WifiPowerSaveGuard); the connect itself is
  // just as beacon-sensitive. Set before begin() so the STA_START handler picks up this value
  // rather than the default. Left off for the session: every network activity here stops the
  // radio outright when it is done, so there is no idle-with-WiFi-up state to save power in.
  WiFi.setSleep(WIFI_PS_NONE);

  // Ranks what the full scan collected, so the strongest AP for the SSID wins. This is what makes
  // dropping the channel/BSSID hint correct on a mesh -- see issueWifiBegin().
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Use stable base MAC so hostname suffix is deterministic across WiFi states.
  uint8_t baseMac[6];
  readDeviceBaseMac(baseMac);
  String hostname = "CrossPoint-Reader-" + formatMacCompact(baseMac);
  WiFi.setHostname(hostname.c_str());

  applyScanBudget();
}

// Bounds how long the connect-time scan may take.
//
// esp_wifi_set_scan_parameters() is explicit that "the values set using this API are also used
// for scans used while connecting", so this is the real knob for the full sweep issueWifiBegin()
// now always performs -- not a workaround.
//
// Worth being precise about what the cost actually depends on: a scan sits on each channel for a
// fixed dwell and collects whatever answers within it, so the duration is CHANNELS x DWELL and is
// independent of how many SSIDs are in the air. A crowded band costs driver memory for the AP
// record list, not time. At the IDF default of 120 ms a 13-channel sweep is ~1560 ms, which is
// most of the ~2.57 s that unhinted associations used to take (the rest being auth/assoc).
//
// 80 ms was tried here and is back out: it produced exactly the miss this comment used to warn
// about -- "EVT disconnected at 1546 ms: reason=201 (NO_AP_FOUND) assoc=0
// bssid=00-00-00-00-00-00 rssi=-128", an all-zero BSSID meaning the sweep never saw the AP at
// all, followed by the driver's own full re-sweep. A miss costs a whole second sweep (~1.5 s),
// three times what the shorter dwell saves, so the trade only paid when it happened to work.
//
// 120 ms is the IDF default and there is a reason it sits above 100: the standard beacon
// interval IS 100 ms, so a dwell shorter than that is not guaranteed to overlap a single beacon
// on the channel -- it leaves the scan depending on catching the probe response alone, and a
// probe response is one frame that can be lost. Do not go below 100 again without a mechanism
// that does not need to hear a beacon.
//
// home_chan_dwell_time is set to its documented 30 ms minimum: it only matters while already
// associated (returning to the home channel between scanned ones), which is not this path.
void WifiSelectionActivity::applyScanBudget() {
  wifi_scan_default_params_t params = {};
  params.scan_time.active.min = 0;
  params.scan_time.active.max = SCAN_ACTIVE_DWELL_MAX_MS;
  params.scan_time.passive = SCAN_PASSIVE_DWELL_MS;
  params.home_chan_dwell_time = SCAN_HOME_CHAN_DWELL_MS;

  // Requires station mode to have been started (returns ESP_FAIL otherwise), which is why this
  // runs at the end of prepareForConnect() rather than alongside the other WiFi.set* calls.
  const esp_err_t err = esp_wifi_set_scan_parameters(&params);
  if (err != ESP_OK) {
    LOG_DBG("WIFI", "Scan budget not applied (%s); driver defaults apply (~120 ms/channel)", esp_err_to_name(err));
    return;
  }
  LOG_DBG("WIFI", "Scan budget: active<=%u ms/chan, passive %u ms, home dwell %u ms", SCAN_ACTIVE_DWELL_MAX_MS,
          SCAN_PASSIVE_DWELL_MS, SCAN_HOME_CHAN_DWELL_MS);
}

void WifiSelectionActivity::issueWifiBegin() {
  wifiEventAttemptAssociated.store(false, std::memory_order_relaxed);

  // Always a full, signal-sorted scan. The cached channel/BSSID hint that used to shortcut this
  // is gone, and the device data is unambiguous about why:
  //
  //  - It never paid. Hinted association measured 2517/2518/2535 ms against unhinted
  //    2569/2571 ms -- ~40 ms apart, because under WIFI_ALL_CHANNEL_SCAN conf.sta.channel cannot
  //    short-circuit the sweep at all.
  //  - Made to work (WIFI_FAST_SCAN), it became a coin flip: 201 ms when the pinned probe hit,
  //    but "EVT disconnected: reason=201 (NO_AP_FOUND)" at 2462 ms when it missed, with the
  //    driver's own retry then associating 150 ms later. That is the whole of the supposed
  //    "flat ~2.5 s association".
  //  - Unpinning the BSSID to fix that broke AP selection instead: WIFI_FAST_SCAN takes the
  //    FIRST match, and setSortMethod() only ranks what a full scan collected. On a mesh SSID it
  //    attached to a -86 dBm AP on channel 1 in place of the -63 dBm one on channel 11.
  //
  // A full scan sees every AP for the SSID and sort-by-signal picks the strongest, which is the
  // only correct answer on a mesh -- and the cost is bounded by applyScanBudget(), not by how
  // many SSIDs are in the air.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);

  // Reset to DHCP in case a previous attempt left a static config behind.
  WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress());

  const char* pwd = (selectedRequiresPassword && !enteredPassword.empty()) ? enteredPassword.c_str() : nullptr;
  const unsigned long preBeginMs = millis() - connectionStartTime;
  LOG_DBG("WIFI", "WiFi.begin -> %s (scan=all-channel sorted-by-signal, pre-begin %lu ms)", selectedSSID.c_str(),
          preBeginMs);
  if (pwd) {
    WiFi.begin(selectedSSID.c_str(), pwd);
  } else {
    WiFi.begin(selectedSSID.c_str());
  }
}

bool WifiSelectionActivity::checkCaptivePortal() {
  // Probe a known HTTP endpoint that returns 204 on open internet.
  // Captive portals intercept this and return a redirect (3xx) or 200 with a login page.
  NetworkClient client;
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(5000);
  const unsigned long probeStart = millis();
  LOG_DBG("WIFI", "Captive portal probe start: dns=%s gw=%s rssi=%d", WiFi.dnsIP().toString().c_str(),
          WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  if (!http.begin(client, "http://connectivitycheck.gstatic.com/generate_204")) {
    LOG_DBG("WIFI", "Captive portal probe setup failed after %lu ms", millis() - probeStart);
    return false;
  }
  const int code = http.GET();
  String location = http.getLocation();
  http.end();

  if (code < 0) {
    LOG_DBG("WIFI", "Captive portal probe failed after %lu ms (connection error %d, dns=%s)", millis() - probeStart,
            code, WiFi.dnsIP().toString().c_str());
    return false;
  }

  LOG_DBG("WIFI", "Captive portal probe completed after %lu ms (HTTP %d)", millis() - probeStart, code);

  if (code == 204) {
    return false;  // Open internet, no captive portal
  }

  // Any redirect or unexpected 200 means a captive portal is intercepting.
  captivePortalUrl = location.length() > 0 ? location.c_str() : "http://connectivitycheck.gstatic.com/generate_204";
  LOG_DBG("WIFI", "Captive portal detected (HTTP %d), URL: %s", code, captivePortalUrl.c_str());
  return true;
}

void WifiSelectionActivity::checkConnectionStatus() {
  if (state != WifiSelectionState::CONNECTING && state != WifiSelectionState::AUTO_CONNECTING &&
      state != WifiSelectionState::AUTO_CYCLING) {
    return;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    // Successfully connected
    IPAddress ip = WiFi.localIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = ipStr;
    autoConnecting = false;

    LOG_DBG("WIFI", "Connected to %s in %lu ms (rssi=%d ch=%d ip=%s gw=%s mask=%s dns=%s)", selectedSSID.c_str(),
            millis() - connectionStartTime, WiFi.RSSI(), WiFi.channel(), ipStr, WiFi.gatewayIP().toString().c_str(),
            WiFi.subnetMask().toString().c_str(), WiFi.dnsIP().toString().c_str());

    // Records what we actually connected to. NOTHING reads the BSSID/channel back to shortcut a
    // later connect any more -- issueWifiBegin() always does a full signal-sorted scan, for the
    // reasons documented there. The whole record is diagnostic now (the "Reset info" prompt below
    // clears it, and the IP profile has been diagnostic-only for a while), so the storage in
    // WifiCredentialStore is a candidate for deletion once the full-scan path has some mileage.
    // Kept for now rather than widening this change into the settings serialisation.
    // SD card operations need the display lock.
    {
      RenderLock lock(*this);
      WIFI_STORE.setLastConnectedSsid(selectedSSID);
      const uint8_t* actualBssid = WiFi.BSSID();
      const int actualChannel = WiFi.channel();
      if (actualBssid && actualChannel > 0 && actualChannel <= 255) {
        const IPAddress gw = WiFi.gatewayIP();
        const IPAddress mask = WiFi.subnetMask();
        const IPAddress dns = WiFi.dnsIP();
        const uint8_t ipBytes[4] = {ip[0], ip[1], ip[2], ip[3]};
        const uint8_t gwBytes[4] = {gw[0], gw[1], gw[2], gw[3]};
        const uint8_t maskBytes[4] = {mask[0], mask[1], mask[2], mask[3]};
        const uint8_t dnsBytes[4] = {dns[0], dns[1], dns[2], dns[3]};
        WIFI_STORE.updateConnectionCache(selectedSSID, actualBssid, static_cast<uint8_t>(actualChannel), ipBytes,
                                         gwBytes, maskBytes, dnsBytes, 0u);
      }
    }

    // Only probe for a captive portal on a network the user just entered
    // credentials for. A saved auto-connect network is one we've reached the
    // internet on before, so the probe can only waste a full HTTP timeout
    // (~5 s, blocking the loop) when DNS/upstream is slow.
    const bool isNewNetwork = !usedSavedPassword && !enteredPassword.empty();
    if (isNewNetwork) {
      // Check for captive portal before declaring success
      if (checkCaptivePortal()) {
        state = WifiSelectionState::CAPTIVE_PORTAL;
        requestUpdate();
        return;
      }

      // We entered a new password, ask if user wants to save it
      state = WifiSelectionState::SAVE_PROMPT;
      savePromptSelection = 0;  // Default to "Yes"
      requestUpdate();
    } else {
      // Using saved password or open network - complete immediately
      LOG_DBG("WIFI",
              "Connected with saved/open credentials, "
              "completing immediately");
      onComplete(true);
    }
    return;
  }

  const unsigned long timeout =
      state == WifiSelectionState::AUTO_CYCLING ? AUTO_CYCLE_TIMEOUT_MS : CONNECTION_TIMEOUT_MS;

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    if (state == WifiSelectionState::AUTO_CONNECTING) {
      // Primary SSID failed — scan and try remaining saved credentials
      autoCycleAfterScan = true;
      startWifiScan();
      return;
    }
    if (state == WifiSelectionState::AUTO_CYCLING) {
      tryNextAutoCycleCandidate();
      return;
    }
    connectionError = tr(STR_ERROR_GENERAL_FAILURE);
    if (status == WL_NO_SSID_AVAIL) {
      connectionError = tr(STR_ERROR_NETWORK_NOT_FOUND);
    }
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }

  // Check for timeout
  if (millis() - connectionStartTime > timeout) {
    WiFi.disconnect();
    if (state == WifiSelectionState::AUTO_CONNECTING) {
      autoCycleAfterScan = true;
      startWifiScan();
      return;
    }
    if (state == WifiSelectionState::AUTO_CYCLING) {
      tryNextAutoCycleCandidate();
      return;
    }
    connectionError = tr(STR_ERROR_CONNECTION_TIMEOUT);
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }
}

void WifiSelectionActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

  // Check scan progress
  if (state == WifiSelectionState::SCANNING) {
    processWifiScanResults();
    return;
  }

  // Check connection progress
  if (state == WifiSelectionState::CONNECTING || state == WifiSelectionState::AUTO_CONNECTING ||
      state == WifiSelectionState::AUTO_CYCLING) {
    checkConnectionStatus();
    return;
  }

  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    // Reach here once password entry finished in subactivity
    attemptConnection();
    return;
  }

  // Handle save prompt state
  if (state == WifiSelectionState::SAVE_PROMPT) {
    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Up) ||
        mappedInput.wasLogicalPressed(MappedInputManager::Direction::Left)) {
      if (savePromptSelection > 0) {
        savePromptSelection--;
        requestUpdate();
      }
    } else if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Down) ||
               mappedInput.wasLogicalPressed(MappedInputManager::Direction::Right)) {
      if (savePromptSelection < 1) {
        savePromptSelection++;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (savePromptSelection == 0) {
        // User chose "Yes" - save the password
        RenderLock lock(*this);
        WIFI_STORE.addCredential(selectedSSID, enteredPassword);
      }
      // Complete - parent will start web server
      onComplete(true);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Skip saving, complete anyway
      onComplete(true);
    }
    return;
  }

  // Handle forget prompt state (connection failed with saved credentials)
  if (state == WifiSelectionState::FORGET_PROMPT) {
    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Up) ||
        mappedInput.wasLogicalPressed(MappedInputManager::Direction::Left)) {
      if (forgetPromptSelection > 0) {
        forgetPromptSelection--;
        requestUpdate();
      }
    } else if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Down) ||
               mappedInput.wasLogicalPressed(MappedInputManager::Direction::Right)) {
      if (forgetPromptSelection < 2) {
        forgetPromptSelection++;
        requestUpdate();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (forgetPromptSelection == 1) {
        RenderLock lock(*this);
        // User chose "Reset info" - drop the recorded BSSID/channel + IP/gw/mask/DNS but keep
        // the saved password. Connects already do a full scan + DHCP unconditionally, so this is
        // now about clearing the diagnostic record rather than changing what the next connect
        // does.
        WIFI_STORE.clearConnectionCache(selectedSSID);
      } else if (forgetPromptSelection == 2) {
        RenderLock lock(*this);
        // User chose "Forget network" - forget the network
        WIFI_STORE.removeCredential(selectedSSID);
        // Update the network list to reflect the change
        const auto network = find_if(networks.begin(), networks.end(),
                                     [this](const WifiNetworkInfo& net) { return net.ssid == selectedSSID; });
        if (network != networks.end()) {
          network->hasSavedPassword = false;
        }
      }
      // Go back to network list (whichever action, including Cancel, was selected)
      startWifiScan();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Skip forgetting, go back to network list
      startWifiScan();
    }
    return;
  }

  // Handle captive portal state - user must authorize on another device
  if (state == WifiSelectionState::CAPTIVE_PORTAL) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // User says they've completed browser auth - proceed as connected
      if (!usedSavedPassword && !enteredPassword.empty()) {
        state = WifiSelectionState::SAVE_PROMPT;
        savePromptSelection = 0;
        requestUpdate();
      } else {
        onComplete(true);
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      WiFi.disconnect();
      startWifiScan();
    }
    return;
  }

  // Handle connected state (should not normally be reached - connection
  // completes immediately)
  if (state == WifiSelectionState::CONNECTED) {
    // Safety fallback - immediately complete
    onComplete(true);
    return;
  }

  // Handle connection failed state
  if (state == WifiSelectionState::CONNECTION_FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // If we were auto-connecting or using a saved credential, offer to forget
      // the network
      if (autoConnecting || usedSavedPassword) {
        autoConnecting = false;
        state = WifiSelectionState::FORGET_PROMPT;
        forgetPromptSelection = 0;  // Default to "Cancel"
      } else {
        // Go back to network list on failure for non-saved credentials
        state = WifiSelectionState::NETWORK_LIST;
      }
      requestUpdate();
      return;
    }
  }

  // Handle network list state
  if (state == WifiSelectionState::NETWORK_LIST) {
    // Check for Back button to exit (cancel)
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onComplete(false);
      return;
    }

    // Check for Confirm button to select network or rescan
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (!networks.empty()) {
        selectNetwork(selectedNetworkIndex);
      } else {
        startWifiScan();
      }
      return;
    }

    if (mappedInput.wasLogicalPressed(MappedInputManager::Direction::Right)) {
      startWifiScan();
      return;
    }

    const bool leftPressed = mappedInput.wasLogicalPressed(MappedInputManager::Direction::Left);
    if (leftPressed) {
      const bool hasSavedPassword = !networks.empty() && networks[selectedNetworkIndex].hasSavedPassword;
      if (hasSavedPassword) {
        selectedSSID = networks[selectedNetworkIndex].ssid;
        state = WifiSelectionState::FORGET_PROMPT;
        forgetPromptSelection = 0;  // Default to "Cancel"
        requestUpdate();
        return;
      }
    }

    // Handle navigation
    // Step on logical Up/Down only: logical Left opens the saved-network options and logical Right
    // rescans (handled above), so they must not also move the selection. The page jump is the
    // double-click on Up/Down.
    buttonNavigator.onNextList(ButtonNavigator::getStepNextButtons(), selectedNetworkIndex,
                               static_cast<int>(networks.size()), [this] { requestUpdate(); });
    buttonNavigator.onPreviousList(ButtonNavigator::getStepPreviousButtons(), selectedNetworkIndex,
                                   static_cast<int>(networks.size()), [this] { requestUpdate(); });
  }
}

std::string WifiSelectionActivity::getSignalStrengthIndicator(const int32_t rssi) const {
  // Convert RSSI to signal bars representation
  if (rssi >= -50) {
    return "||||";  // Excellent
  }
  if (rssi >= -60) {
    return " |||";  // Good
  }
  if (rssi >= -70) {
    return "  ||";  // Fair
  }
  return "   |";  // Very weak
}

void WifiSelectionActivity::render(RenderLock&&) {
  if (state == WifiSelectionState::PASSWORD_ENTRY) {
    return;
  }
  renderer.clearScreen();
  renderUi();
  afterUiRender();
  renderer.displayBuffer();
}

void WifiSelectionActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<WifiSelectionActivity*>(user)->buildScreen(screen);
}

void WifiSelectionActivity::actionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  auto* self = static_cast<WifiSelectionActivity*>(user);
  switch (event.action) {
    case ACTION_ROW:
      self->selectNetwork(event.value);
      break;
    case ACTION_BACK:
      if (self->state == WifiSelectionState::NETWORK_LIST) {
        self->onComplete(false);
      } else if (self->state == WifiSelectionState::CAPTIVE_PORTAL) {
        WiFi.disconnect();
        self->startWifiScan();
      } else {
        self->startWifiScan();
      }
      break;
    case ACTION_RESCAN:
      self->startWifiScan();
      break;
    case ACTION_OPTIONS: {
      const bool hasSaved = !self->networks.empty() && self->networks[self->selectedNetworkIndex].hasSavedPassword;
      if (hasSaved) {
        self->selectedSSID = self->networks[self->selectedNetworkIndex].ssid;
        self->state = WifiSelectionState::FORGET_PROMPT;
        self->forgetPromptSelection = 0;
        self->requestUpdate();
      }
      break;
    }
    case ACTION_PROMPT_YES:
      WIFI_STORE.addCredential(self->selectedSSID, self->enteredPassword);
      self->onComplete(true);
      break;
    case ACTION_PROMPT_NO:
      self->onComplete(true);
      break;
    case ACTION_FORGET_CANCEL:
      self->startWifiScan();
      break;
    case ACTION_FORGET_RESET:
      WIFI_STORE.clearConnectionCache(self->selectedSSID);
      self->startWifiScan();
      break;
    case ACTION_FORGET_CONFIRM:
      WIFI_STORE.removeCredential(self->selectedSSID);
      {
        const auto it = std::find_if(self->networks.begin(), self->networks.end(),
                                     [self](const WifiNetworkInfo& net) { return net.ssid == self->selectedSSID; });
        if (it != self->networks.end()) {
          it->hasSavedPassword = false;
        }
      }
      self->startWifiScan();
      break;
    case ACTION_FAILED_DONE:
      if (self->autoConnecting || self->usedSavedPassword) {
        self->autoConnecting = false;
        self->state = WifiSelectionState::FORGET_PROMPT;
        self->forgetPromptSelection = 0;
      } else {
        self->state = WifiSelectionState::NETWORK_LIST;
      }
      self->requestUpdate();
      break;
    case ACTION_CAPTIVE_DONE:
      if (!self->usedSavedPassword && !self->enteredPassword.empty()) {
        self->state = WifiSelectionState::SAVE_PROMPT;
        self->savePromptSelection = 0;
        self->requestUpdate();
      } else {
        self->onComplete(true);
      }
      break;
  }
}

void WifiSelectionActivity::syncListViewport(UiScreen& screen, freeink::ui::ListProps& props) {
  props = screen.resolveListProps(props);
  nav.syncToProps(screen.body(), props.rowHeight, props.rowGap, static_cast<int>(networks.size()), props);
}

void WifiSelectionActivity::materializeListWindow() {
  namespace fui = freeink::ui;
  const int count = static_cast<int>(networks.size());
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(nav.top, count)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(count - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));

  for (uint16_t i = 0; i < windowCount; ++i) {
    const int itemIndex = windowFirst + i;
    const auto& net = networks[itemIndex];
    windowLabels[i] = net.ssid;
    windowValues[i] = std::string(net.hasSavedPassword ? "+ " : "") +
                      (net.isEncrypted ? "* " : "") +
                      getSignalStrengthIndicator(net.rssi);

    freeink::ui::ListItem& item = windowItems[i];
    item = {};
    item.label = windowLabels[i].c_str();
    item.value = windowValues[i].c_str();
    item.actionValue = static_cast<int16_t>(itemIndex);
    item.enabled = true;
    item.state = (itemIndex == selectedNetworkIndex) ? fui::StateSelected : fui::StateNormal;
  }
}

void WifiSelectionActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;

  char countStr[64];
  snprintf(countStr, sizeof(countStr), tr(STR_NETWORKS_FOUND), networks.size());

  switch (state) {
    case WifiSelectionState::NETWORK_LIST: {
      screen.header(tr(STR_WIFI_NETWORKS), cachedMacAddress.c_str());

      fui::FooterAction footerActions[3];
      uint8_t footerCount = 0;
      footerActions[footerCount++] = {tr(STR_BACK), ACTION_BACK};
      footerActions[footerCount++] = {tr(STR_RETRY), ACTION_RESCAN};
      const bool hasSaved = !networks.empty() && networks[selectedNetworkIndex].hasSavedPassword;
      if (hasSaved) {
        footerActions[footerCount++] = {tr(STR_OPTIONS_BUTTON), ACTION_OPTIONS};
      }
      screen.footer(footerActions, footerCount);

      if (networks.empty()) {
        fui::TextStyle msgStyle = screen.theme().bodyText;
        msgStyle.align = fui::TextAlign::Center;
        const int16_t msgH = screen.target().lineHeight(msgStyle.font);

        fui::TextStyle hintStyle = screen.theme().smallText;
        hintStyle.align = fui::TextAlign::Center;
        const int16_t hintH = screen.target().lineHeight(hintStyle.font);

        const int16_t gap = screen.theme().spaceSm;
        const int16_t totalH = static_cast<int16_t>(msgH + gap + hintH);
        const int16_t topMargin = static_cast<int16_t>((screen.body().height - totalH) / 2);
        if (topMargin > 0) {
          screen.spacer(topMargin);
        }
        screen.target().text(screen.takeTop(msgH), tr(STR_NO_NETWORKS), msgStyle);
        screen.spacer(gap);
        screen.target().text(screen.takeTop(hintH), tr(STR_PRESS_OK_SCAN), hintStyle);
      } else {
        fui::ListProps props;
        props.count = static_cast<uint16_t>(networks.size());
        props.action = ACTION_ROW;
        props.inputMask = fui::InputTouch;
        props.labelText = screen.theme().bodyText;
        props.valueText = screen.theme().smallText;

        syncListViewport(screen, props);
        materializeListWindow();

        props.items = windowItems.data();
        props.itemsWindowFirst = windowFirst;
        props.itemsWindowCount = windowCount;
        screen.list(props);
      }
      break;
    }
    case WifiSelectionState::SCANNING: {
      screen.header(tr(STR_WIFI_NETWORKS), cachedMacAddress.c_str());
      fui::TextStyle titleStyle = screen.theme().titleText;
      titleStyle.bold = true;
      titleStyle.align = fui::TextAlign::Center;
      screen.centeredText(tr(STR_SCANNING), titleStyle);
      break;
    }
    case WifiSelectionState::AUTO_CONNECTING:
    case WifiSelectionState::AUTO_CYCLING:
    case WifiSelectionState::CONNECTING: {
      screen.header(tr(STR_WIFI_NETWORKS), cachedMacAddress.c_str());
      fui::TextStyle titleStyle = screen.theme().titleText;
      titleStyle.bold = true;
      titleStyle.align = fui::TextAlign::Center;
      const int16_t titleH = screen.target().lineHeight(titleStyle.font);

      fui::TextStyle infoStyle = screen.theme().bodyText;
      infoStyle.align = fui::TextAlign::Center;
      const int16_t infoH = screen.target().lineHeight(infoStyle.font);

      std::string ssidInfo = std::string(tr(STR_TO_PREFIX)) + selectedSSID;
      if (ssidInfo.length() > 25) {
        ssidInfo.replace(22, ssidInfo.length() - 22, "...");
      }

      const int16_t gap = screen.theme().spaceMd;
      const int16_t totalH = static_cast<int16_t>(titleH + gap + infoH);
      const int16_t topMargin = static_cast<int16_t>((screen.body().height - totalH) / 2);
      if (topMargin > 0) {
        screen.spacer(topMargin);
      }
      screen.target().text(screen.takeTop(titleH), tr(STR_CONNECTING), titleStyle);
      screen.spacer(gap);
      screen.target().text(screen.takeTop(infoH), ssidInfo.c_str(), infoStyle);
      break;
    }
    case WifiSelectionState::CONNECTED: {
      screen.header(tr(STR_WIFI_NETWORKS), cachedMacAddress.c_str());
      fui::FooterAction footerActions[1];
      footerActions[0] = {tr(STR_DONE), ACTION_BACK};
      screen.footer(footerActions, 1);

      fui::TextStyle titleStyle = screen.theme().titleText;
      titleStyle.bold = true;
      titleStyle.align = fui::TextAlign::Center;
      const int16_t titleH = screen.target().lineHeight(titleStyle.font);

      fui::TextStyle infoStyle = screen.theme().bodyText;
      infoStyle.align = fui::TextAlign::Center;
      const int16_t infoH = screen.target().lineHeight(infoStyle.font);

      std::string ssidInfo = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
      std::string ipInfo = std::string(tr(STR_IP_ADDRESS_PREFIX)) + connectedIP;

      const int16_t gap = screen.theme().spaceSm;
      const int16_t totalH = static_cast<int16_t>(titleH + gap * 2 + infoH * 2);
      const int16_t topMargin = static_cast<int16_t>((screen.body().height - totalH) / 2);
      if (topMargin > 0) {
        screen.spacer(topMargin);
      }
      screen.target().text(screen.takeTop(titleH), tr(STR_CONNECTED), titleStyle);
      screen.spacer(gap);
      screen.target().text(screen.takeTop(infoH), ssidInfo.c_str(), infoStyle);
      screen.spacer(gap);
      screen.target().text(screen.takeTop(infoH), ipInfo.c_str(), infoStyle);
      break;
    }
    case WifiSelectionState::SAVE_PROMPT: {
      std::string title = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
      ConfirmDialog::Spec spec;
      spec.title = title.c_str();
      spec.headline = tr(STR_CONNECTED);
      spec.message = tr(STR_SAVE_PASSWORD);
      spec.cancelLabel = tr(STR_NO);
      spec.cancelAction = ACTION_PROMPT_NO;
      spec.acceptLabel = tr(STR_YES);
      spec.acceptAction = ACTION_PROMPT_YES;
      ConfirmDialog::draw(screen, spec);
      break;
    }
    case WifiSelectionState::FORGET_PROMPT: {
      std::string title = std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID;
      fui::DialogOption options[3];
      options[0] = {tr(STR_CANCEL), ACTION_FORGET_CANCEL};
      options[1] = {tr(STR_RESET_INFO_BUTTON), ACTION_FORGET_RESET};
      options[2] = {tr(STR_FORGET_BUTTON), ACTION_FORGET_CONFIRM};

      fui::OptionDialogProps props;
      props.title = title.c_str();
      props.headline = tr(STR_NETWORK_OPTIONS);
      props.options = options;
      props.optionCount = 3;
      props.verticalOptions = true;
      props.titleText = screen.theme().smallText;
      props.titleText.align = fui::TextAlign::Center;
      props.headlineText = screen.theme().titleText;
      props.headlineText.bold = true;
      props.headlineText.align = fui::TextAlign::Center;
      props.buttonText = screen.theme().bodyText;
      props.buttonText.bold = true;
      props.buttonText.align = fui::TextAlign::Center;
      props.buttonHeight = screen.theme().minTouchSize;
      props.gap = screen.theme().spaceMd;

      screen.dialog(props);
      break;
    }
    case WifiSelectionState::CONNECTION_FAILED: {
      ConfirmDialog::Spec spec;
      spec.headline = tr(STR_CONNECTION_FAILED);
      spec.message = connectionError.empty() ? nullptr : connectionError.c_str();
      spec.acceptLabel = tr(STR_BACK);
      spec.acceptAction = ACTION_FAILED_DONE;
      ConfirmDialog::draw(screen, spec);
      break;
    }
    case WifiSelectionState::CAPTIVE_PORTAL: {
      screen.header(tr(STR_CAPTIVE_PORTAL_DETECTED), cachedMacAddress.c_str());
      fui::FooterAction footerActions[2];
      footerActions[0] = {tr(STR_BACK), ACTION_BACK};
      footerActions[1] = {tr(STR_CAPTIVE_PORTAL_DONE), ACTION_CAPTIVE_DONE};
      screen.footer(footerActions, 2);

      fui::TextStyle hintStyle = screen.theme().bodyText;
      hintStyle.align = fui::TextAlign::Center;
      hintStyle.maxLines = 2;
      const int16_t hintH = static_cast<int16_t>(screen.target().lineHeight(hintStyle.font) * 2);

      constexpr int16_t QR_SIZE = 220;

      fui::TextStyle urlStyle = screen.theme().smallText;
      urlStyle.align = fui::TextAlign::Center;
      const int16_t urlH = screen.target().lineHeight(urlStyle.font);

      const int16_t gap = screen.theme().spaceMd;
      const int16_t totalH = static_cast<int16_t>(hintH + gap + QR_SIZE + gap + urlH);
      const int16_t topMargin = static_cast<int16_t>((screen.body().height - totalH) / 2);
      if (topMargin > 0) {
        screen.spacer(topMargin);
      }

      std::string hintText = std::string(tr(STR_CAPTIVE_PORTAL_HINT_1)) + " " + tr(STR_CAPTIVE_PORTAL_HINT_2);
      screen.target().text(screen.takeTop(hintH), hintText.c_str(), hintStyle);

      screen.spacer(gap);
      captiveQrRect_ = fui::centeredRect(screen.takeTop(QR_SIZE), fui::Size{QR_SIZE, QR_SIZE});

      screen.spacer(gap);
      screen.target().text(screen.takeTop(urlH), captivePortalUrl.c_str(), urlStyle);
      break;
    }
    case WifiSelectionState::PASSWORD_ENTRY:
      break;
  }
}

void WifiSelectionActivity::afterUiRender() {
  if (state == WifiSelectionState::CAPTIVE_PORTAL && captiveQrRect_.width > 0) {
    QrUtils::drawQrCode(renderer,
                        Rect{captiveQrRect_.x, captiveQrRect_.y, captiveQrRect_.width, captiveQrRect_.height},
                        captivePortalUrl);
  }
}

void WifiSelectionActivity::onComplete(const bool connected) {
  ActivityResult result;
  result.isCancelled = !connected;
  if (connected) {
    result.data = WifiResult{true, selectedSSID, connectedIP};
  }
  setResult(std::move(result));
  finish();
}
