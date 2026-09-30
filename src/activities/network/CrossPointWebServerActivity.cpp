#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalSystem.h>  // feedWatchdog()
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstddef>

#include "MappedInputManager.h"
#include "NetworkModeSelectionActivity.h"
#include "SdCardFontGlobals.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/SignalStrengthWidget.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID = "Witchling-Reader";
constexpr const char* AP_PASSWORD = nullptr;  // Open network for ease of use
constexpr const char* AP_HOSTNAME = "witchling";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 2;  // reduce from default 4 to save resources
// Fixed AP addressing (the esp32 softAP default, pinned explicitly) so the
// URL screen can be painted before the WiFi stack starts.
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

// DNS server for captive portal (redirects all DNS queries to our IP)
DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;

void stopDnsServer() {
  if (!dnsServer) {
    return;
  }
  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}
}  // namespace

void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("WEBACT", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Reset state
  state = WebServerActivityState::MODE_SELECTION;
  networkMode = NetworkMode::JOIN_NETWORK;
  isApMode = false;
  webServerStarted = false;
  buffersReleased = false;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;

  resetUi();
  app.setScreen(screenTrampoline, this);
  app.on(ACTION_BACK, actionTrampoline, this);

  requestUpdate();

  // Launch network mode selection subactivity
  LOG_DBG("WEBACT", "Launching NetworkModeSelectionActivity...");
  startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             onGoHome();
                           } else {
                             onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                           }
                         });
}

void CrossPointWebServerActivity::onExit() {
  resetUi();
  Activity::onExit();

  state = WebServerActivityState::SHUTTING_DOWN;
  stopDnsServer();
  MDNS.end();

  // Skip reboot if WiFi was never activated (e.g. user backed out of mode selection).
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    if (isApMode) {
      WiFi.softAPdisconnect(true);
    } else {
      WiFi.disconnect(false);
    }
    delay(30);
    silentRestart();
  }
}

void CrossPointWebServerActivity::onNetworkModeSelected(const NetworkMode mode) {
  const char* modeName = (mode == NetworkMode::CREATE_HOTSPOT) ? "Create Hotspot" : "Join Network";
  LOG_DBG("WEBACT", "Network mode selected: %s", modeName);

  networkMode = mode;
  isApMode = (mode == NetworkMode::CREATE_HOTSPOT);

  if (mode == NetworkMode::JOIN_NETWORK) {
    // STA mode - launch WiFi selection
    LOG_DBG("WEBACT", "Turning on WiFi (STA mode)...");
    // BEFORE the radio: association itself is allocation-heavy, and on a lean heap it is the
    // join that fails, not the server that follows it.
    freeMemoryBeforeRadio();
    powerManager.ensureFullSpeedForRadio();  // WiFi needs >=80 MHz; see HalPowerManager
    WiFi.mode(WIFI_STA);

    state = WebServerActivityState::WIFI_SELECTION;
    LOG_DBG("WEBACT", "Launching WifiSelectionActivity...");
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& wifi = std::get<WifiResult>(result.data);
                               connectedIP = wifi.ip;
                               connectedSSID = wifi.ssid;
                             }
                             onWifiSelectionComplete(!result.isCancelled);
                           });
  } else {
    // AP mode - start access point
    state = WebServerActivityState::AP_STARTING;
    requestUpdate();
    startAccessPoint();
  }
}

void CrossPointWebServerActivity::onWifiSelectionComplete(const bool connected) {
  LOG_DBG("WEBACT", "WifiSelectionActivity completed, connected=%d", connected);

  if (connected) {
    // Get connection info before exiting subactivity
    isApMode = false;

    // Start mDNS for hostname resolution
    if (MDNS.begin(AP_HOSTNAME)) {
      LOG_DBG("WEBACT", "mDNS started: http://%s.local/", AP_HOSTNAME);
    }

    // Start the web server
    startWebServer();
  } else {
    // User cancelled - go back to mode selection
    state = WebServerActivityState::MODE_SELECTION;

    startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               onGoHome();
                             } else {
                               onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                             }
                           });
  }
}

void CrossPointWebServerActivity::startAccessPoint() {
  LOG_DBG("WEBACT", "Starting Access Point mode...");

  // Everything on the server screen is known before WiFi starts: the SSID is a
  // constant and the AP IP is pinned via softAPConfig below. Paint and release
  // the frame buffers (~100KB) first so the WiFi stack + mDNS + DNS server
  // (~66KB) start against a roomy heap; painting after AP start left the X3
  // with <5KB free and aborted in the QR render.
  connectedSSID = AP_SSID;
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
  connectedIP = ipStr;
  showServerScreenAndReleaseBuffers();

  LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());

  // Configure and start the AP
  powerManager.ensureFullSpeedForRadio();  // WiFi needs >=80 MHz; see HalPowerManager
  WiFi.mode(WIFI_AP);
  delay(100);

  // Start soft AP
  bool apStarted;
  if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  } else {
    // Open network (no password)
    apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  }

  if (!apStarted) {
    LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
    onGoHome();
    return;
  }

  // Pin the AP address to what the already-painted screen promises.
  if (!WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK)) {
    LOG_ERR("WEBACT", "ERROR: softAPConfig failed");
  }

  delay(100);  // Wait for AP to fully initialize

  LOG_DBG("WEBACT", "Access Point started!");
  LOG_DBG("WEBACT", "SSID: %s", AP_SSID);
  LOG_DBG("WEBACT", "IP: %s", connectedIP.c_str());

  // Start mDNS for hostname resolution
  if (MDNS.begin(AP_HOSTNAME)) {
    LOG_DBG("WEBACT", "mDNS started: http://%s.local/", AP_HOSTNAME);
  } else {
    LOG_DBG("WEBACT", "WARNING: mDNS failed to start");
  }

  // Start DNS server for captive portal behavior
  // This redirects all DNS queries to our IP, making any domain typed resolve to us
  stopDnsServer();
  // Raw nothrow new: owned by the module-scope dnsServer pointer, freed in
  // stopDnsServer(). On OOM the captive portal degrades to direct IP/mDNS access.
  dnsServer = new (std::nothrow) DNSServer();
  if (dnsServer) {
    dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer->start(DNS_PORT, "*", AP_IP);
    LOG_DBG("WEBACT", "DNS server started for captive portal");
  } else {
    LOG_ERR("WEBACT", "OOM allocating DNS server, captive portal disabled");
  }

  LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());

  // Start the web server
  startWebServer();
}

// Everything that frees memory but needs nothing from the network. Split out of
// showServerScreenAndReleaseBuffers() because that one cannot run until the join succeeded —
// it paints the assigned IP — and waiting until then meant the STA path brought the radio up
// on an untrimmed heap and failed to associate at all on an X3.
void CrossPointWebServerActivity::freeMemoryBeforeRadio() {
  if (memoryFreedForRadio) return;
  memoryFreedForRadio = true;

  // A loaded SD font (Literata etc.) holds ~24-60KB of kern/ligature/glyph data that is
  // completely unused during a web server session. The device restarts when the session ends
  // (silentRestart in onExit), so it is reloaded fresh on the next boot anyway.
  LOG_DBG("WEBACT", "Free heap before SD font unload: %d bytes", ESP.getFreeHeap());
  sdFontSystem.unload(renderer);
  LOG_DBG("WEBACT", "Free heap after SD font unload: %d bytes", ESP.getFreeHeap());

  // The secondary framebuffer and the glyph cache, on the same reasoning as every other
  // network activity. WifiSelectionActivity does this too; both are idempotent, and this call
  // covers the AP path, which never goes through that activity.
  trimMemoryForNetworkSession(renderer, "WEBACT");
}

void CrossPointWebServerActivity::showServerScreenAndReleaseBuffers() {
  // The AP path reaches this before WiFi.mode(WIFI_AP), so here it does the real work; the STA
  // path already ran it before the join and this is a no-op. Either way the memory is gone
  // before the radio, which is the invariant worth keeping.
  freeMemoryBeforeRadio();

  // Set running state now so the paint below uses the correct UI branch.
  state = WebServerActivityState::SERVER_RUNNING;
  if (!isApMode) {
    currentRssi = WiFi.RSSI();
    lastRssiUpdateTime = millis();
  }

  // Paint the QR / URL screen while both frame buffers are still available,
  // then release them. The e-ink controller retains the image in its own RAM —
  // no framebuffer needed after displayBuffer().
  LOG_DBG("WEBACT", "Free heap before frame buffer release: %d bytes", ESP.getFreeHeap());
  renderer.clearScreen();
  renderUi();
  afterUiRender();
  renderer.displayBuffer();
  buffersReleased = true;
  renderer.releaseFrameBuffers();
  LOG_DBG("WEBACT", "Free heap after frame buffer release: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServerActivity::startWebServer() {
  LOG_DBG("WEBACT", "Starting web server...");

  // Both paths have already freed the memory the radio needed (freeMemoryBeforeRadio). What is
  // left is the final paint plus releasing BOTH buffers, and only that needs the assigned IP,
  // which is why the STA path reaches it here rather than before the join.
  if (!buffersReleased) {
    showServerScreenAndReleaseBuffers();
  }

  // Create the web server instance
  webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!webServer) {
    LOG_ERR("WEBACT", "OOM allocating web server");
    onGoHome();
    return;
  }
  webServer->begin();

  if (webServer->isRunning()) {
    webServerStarted = true;
    LOG_DBG("WEBACT", "Web server started successfully");
  } else {
    LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
    webServer.reset();
    // Go back on error
    onGoHome();
  }
}

void CrossPointWebServerActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) return;
  }

  // Handle different states
  if (state == WebServerActivityState::SERVER_RUNNING) {
    // Handle DNS requests for captive portal (AP mode only)
    if (isApMode && dnsServer) {
      dnsServer->processNextRequest();
    }

    // STA mode: Monitor WiFi connection health
    if (!isApMode && webServer && webServer->isRunning()) {
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 2000) {  // Check every 2 seconds
        lastWifiCheck = millis();
        const wl_status_t wifiStatus = WiFi.status();
        if (wifiStatus != WL_CONNECTED) {
          LOG_DBG("WEBACT", "WiFi disconnected! Status: %d", wifiStatus);
          // Show error and exit gracefully
          state = WebServerActivityState::SHUTTING_DOWN;
          requestUpdate();
          return;
        }
        // Log weak signal warnings
        const int rssi = WiFi.RSSI();
        if (rssi < -75) {
          LOG_DBG("WEBACT", "Warning: Weak WiFi signal: %d dBm", rssi);
        }
      }

      // RSSI indicator update intentionally removed: the 640ms e-ink refresh
      // every 5 seconds blocked handleClient, fragmented heap, and gave no
      // meaningful benefit since the user is interacting with the browser, not
      // watching the device screen.
    }

    // Handle web server requests - maximize throughput with watchdog safety
    if (webServer && webServer->isRunning()) {
      const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;

      // Log if there's a significant gap between handleClient calls (>100ms)
      if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
        LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
      }

      // Reset watchdog BEFORE processing - HTTP header parsing can be slow
      HalSystem::feedWatchdog();

      // Process HTTP requests in tight loop for maximum throughput
      // More iterations = more data processed per main loop cycle
      constexpr int MAX_ITERATIONS = 500;
      for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
        webServer->handleClient();
        // Reset watchdog every 32 iterations
        if ((i & 0x1F) == 0x1F) {
          HalSystem::feedWatchdog();
        }
        // Yield and check for exit button every 64 iterations
        if ((i & 0x3F) == 0x3F) {
          yield();
          // Force trigger an update of which buttons are being pressed so be have accurate state
          // for back button checking
          mappedInput.update();
          // Check for exit button inside loop for responsiveness
          if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            onGoHome();
            return;
          }
        }
      }
      lastHandleClientTime = millis();
    }

    // Handle exit on Back button (also check outside loop)
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }
  }
}

void CrossPointWebServerActivity::render(RenderLock&&) {
  if (buffersReleased) return;
  if (state == WebServerActivityState::SERVER_RUNNING || state == WebServerActivityState::AP_STARTING) {
    renderer.clearScreen();
    renderUi();
    afterUiRender();
    renderer.displayBuffer();
  }
}

void CrossPointWebServerActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<CrossPointWebServerActivity*>(user)->buildScreen(screen);
}

void CrossPointWebServerActivity::actionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  auto* self = static_cast<CrossPointWebServerActivity*>(user);
  if (event.action == ACTION_BACK) {
    self->onGoHome();
  }
}

void CrossPointWebServerActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;

  const char* title = isApMode ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER);
  const char* subtitle = connectedSSID.empty() ? nullptr : connectedSSID.c_str();
  screen.header(title, subtitle);

  screen.takeBottom(screen.theme().footerHeight);

  if (state == WebServerActivityState::AP_STARTING) {
    screen.spacer(40);
    screen.centeredText(tr(STR_STARTING_HOTSPOT));
  }

  bodyRect_ = screen.body();
}

void CrossPointWebServerActivity::afterUiRender() {
  if (state == WebServerActivityState::SERVER_RUNNING) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect contentRect{bodyRect_.x, bodyRect_.y, bodyRect_.width, bodyRect_.height};

    if (isApMode) {
      // AP mode display
      int startY = contentRect.y + contentRect.height / 8;

      // Section 1: Connect to WiFi
      renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_CONNECT_WIFI_HINT), true, EpdFontFamily::BOLD);
      startY += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_12_FONT_ID, startY, connectedSSID.c_str(), true, EpdFontFamily::BOLD);

      // Section 2: Open URL
      startY += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 6;
      renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
      startY += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;

      std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local/";
      std::string ipUrl = tr(STR_OR_HTTP_PREFIX) + connectedIP + "/";

      renderer.drawCenteredText(UI_12_FONT_ID, startY, hostnameUrl.c_str(), true, EpdFontFamily::BOLD);
      startY += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
      renderer.drawCenteredText(UI_10_FONT_ID, startY, ipUrl.c_str(), true);

      // Bottom status bar
      const int signalHeight = 22;
      const int signalWidth = contentRect.width - metrics.contentSidePadding * 2;
      const int signalY = contentRect.y + contentRect.height - 70;
      drawWifiSignalStrength(renderer, contentRect.x + metrics.contentSidePadding, signalY, signalWidth, signalHeight, 0);
      renderer.drawCenteredText(SMALL_FONT_ID, signalY + signalHeight + 2, tr(STR_HOTSPOT_MODE));
    } else {
      // STA mode display
      int startY = contentRect.y + contentRect.height / 5;

      renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
      startY += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;

      // Show web server URL prominently
      std::string webInfo = "http://" + connectedIP + "/";
      renderer.drawCenteredText(UI_12_FONT_ID, startY, webInfo.c_str(), true, EpdFontFamily::BOLD);
      startY += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

      // Also show hostname URL
      std::string hostnameUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + AP_HOSTNAME + ".local/";
      renderer.drawCenteredText(UI_10_FONT_ID, startY, hostnameUrl.c_str(), true);

      // Bottom status bar
      const int signalHeight = 22;
      const int signalWidth = contentRect.width - metrics.contentSidePadding * 2;
      const int signalY = contentRect.y + contentRect.height - 70;
      drawWifiSignalStrength(renderer, contentRect.x + metrics.contentSidePadding, signalY, signalWidth, signalHeight,
                             currentRssi);
      renderer.drawCenteredText(SMALL_FONT_ID, signalY + signalHeight + 2, rssiLabel(currentRssi).c_str(), true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
