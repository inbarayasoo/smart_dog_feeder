#include <WiFi.h>
#include <WiFiManager.h>
#include "WifiConnector.h"
#include <Arduino.h>
#include "Secrets.h"

// --- DEVICE IDENTIFICATION ---
const char* DEVICE_ID = "PET_FEEDER_001";

// --- WIFI CONFIGURATION ---
const char* AP_SSID = "Feeder_Setup";

const unsigned long TIMEOUT_MS = 30000; // ms

// ---- runtime reconnect throttle ----
static unsigned long lastReconnectAttemptMs = 0;
static const unsigned long RECONNECT_INTERVAL_MS = 5000; // try every 5s

// WiFi event handler (keeps logs + lets you react if needed)
static void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print(" WiFi got IP: ");
      Serial.println(WiFi.localIP());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println(" WiFi disconnected");
      break;

    default:
      break;
  }
}

// Function to run when AP mode is enabled (for user feedback)
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("--- Entering WiFi Configuration Portal ---");
  Serial.print("Connect to Wi-Fi network: ");
  Serial.println(AP_SSID);
  Serial.print("IP Address to visit: ");
  Serial.println(WiFi.softAPIP());
}

/**
 * BOOT / DEMO MODE:
 * Always show portal, always ask for credentials.
 * (This matches requirement for setup()).
 */
bool setupWiFiProvisioning() {
  WiFi.onEvent(onWiFiEvent);

  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(TIMEOUT_MS / 1000);


  wm.setBreakAfterConfig(true);

  Serial.println("DEMO BOOT: Opening config portal (always) ...");

 
  bool ok = wm.startConfigPortal(AP_SSID, AP_PASS);
  if (!ok) {
    Serial.println("Config portal timed out / failed");
    return false;
  }

  Serial.println("------------------------------------");
  Serial.println("WiFi Connected Successfully!");
  Serial.print("Device ID: ");
  Serial.println(DEVICE_ID);
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("------------------------------------");

  return true;
}

/**
 * RUNTIME MODE:
 * Make ESP behave like a phone:
 * - keep STA mode
 * - enable auto reconnect
 * - do NOT open portal
 */
void wifiEnableAutoReconnect() {
  WiFi.onEvent(onWiFiEvent);

  WiFi.mode(WIFI_STA);

  // These are the key “phone-like” flags:
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // If creds exist in NVS (saved by WiFiManager), this works:
  // - begin() with no args uses stored creds
  WiFi.begin();

  Serial.println(" Auto-reconnect enabled (STA).");
}

/**
 * Call this in loop() when WiFi is down.
 * It will periodically trigger begin() again (non-blocking).
 */
void wifiAutoReconnectTick() {
  if (WiFi.status() == WL_CONNECTED) return;

  const unsigned long nowMs = millis();
  if (lastReconnectAttemptMs != 0 &&
      (nowMs - lastReconnectAttemptMs) < RECONNECT_INTERVAL_MS) {
    return;
  }

  lastReconnectAttemptMs = nowMs;

  WiFi.mode(WIFI_STA);
  WiFi.begin(); // uses saved credentials
}

/**
 * IMPORTANT:
 * Do NOT rely on a manual boolean. Return real status.
 */
bool isConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

void wipeCreds(){ // for the demo, we wipe at boot the network credentials
  WiFi.disconnect(true, true);  // wipe ESP32 WiFi credentials
  delay(100);

  WiFiManager wm;
  wm.resetSettings();           // wipe WiFiManager credentials
  return;
}
