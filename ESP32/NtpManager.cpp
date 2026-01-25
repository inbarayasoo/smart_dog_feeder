// NtpManager.cpp

#include "NtpManager.h"
#include <time.h>
#include <Arduino.h>
#include <WiFi.h>  
#include "Secrets.h"

// --- Time Configuration ---
const char* NTP_SERVER   = "pool.ntp.org";
const char* NTP_SERVER_2 = "ntp.technion.ac.il";

// UTC+2 offset (seconds)
static const long GMT_OFFSET_SEC = 7200;
static const int  DST_OFFSET_SEC = 0;

// Define "valid time" similarly to your main (epoch large enough)
static bool timeIsValidNow() {
  time_t now = time(nullptr);
  return (now >= 100000);
}

// -------------------- NON-BLOCKING NTP (for loop) --------------------
static const char* kServers[] = { NTP_SERVER_2, NTP_SERVER };
static const int kNumServers = 2;

static bool ntpInProgress = false;
static int ntpServerIdx = 0;
static unsigned long ntpServerStartMs = 0;

// how long to wait per server before trying next (non-blocking)
static const unsigned long NTP_SERVER_TIMEOUT_MS = 1000;

static void startNtpOnServer(int idx) { //get time from ntp server
  const char* server = kServers[idx];
  Serial.print(" NTP: starting sync with ");
  Serial.println(server);

  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, server);
  ntpServerStartMs = millis();
}

bool ntpTick() { // get time in a non blocking fashion
  // If already valid, nothing to do
  if (timeIsValidNow()) return true;

  // Need WiFi
  if (WiFi.status() != WL_CONNECTED) {
    ntpInProgress = false;
    return false;
  }

  // Kick off if not running
  if (!ntpInProgress) {
    ntpInProgress = true;
    ntpServerIdx = 0;
    startNtpOnServer(ntpServerIdx);
    return false;
  }

  // Check if time became valid
  if (timeIsValidNow()) {
    ntpInProgress = false;
    Serial.println(" NTP: time is valid now");
    printCurrentTime();
    return true;
  }

  // Timeout on this server -> try next
  if (millis() - ntpServerStartMs >= NTP_SERVER_TIMEOUT_MS) {
    ntpServerIdx++;
    if (ntpServerIdx >= kNumServers) {
      Serial.println(" NTP: failed on all servers (non-blocking)");
      ntpInProgress = false;
      return false;
    }
    startNtpOnServer(ntpServerIdx);
  }

  return false; // still trying
}

// -------------------- BLOCKING NTP (for setup only) --------------------
static bool tryNtpServerBlocking(const char* server) { //get time in a blocking fashion for setup
  Serial.print("Trying NTP server (blocking): ");
  Serial.println(server);

  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, server);

  int attempts = 0;

  // ~10 seconds total
  while (!timeIsValidNow() && attempts < 20) {
    delay(500);
    attempts++;
  }

  return timeIsValidNow();
}

bool initNTP() { //initiallize connection to ntp server
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: WiFi not connected. Cannot initialize NTP.");
    return false;
  }

  Serial.println("Setting up NTP time synchronization (blocking)...");

  if (tryNtpServerBlocking(NTP_SERVER_2)) {
    Serial.println("NTP Time Synchronization Successful (secondary).");
    printCurrentTime();
    return true;
  }

  if (tryNtpServerBlocking(NTP_SERVER)) {
    Serial.println("NTP Time Synchronization Successful (primary).");
    printCurrentTime();
    return true;
  }

  Serial.println("Failed to get time from all NTP servers.");
  return false;
}

/**
 * @brief Prints the current local time to the Serial Monitor.
 */
void printCurrentTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.print("Current Local Time: ");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  } else {
    Serial.println("Local time not available.");
  }
}

/**
 * @brief Utility function to get the current time structure.
 */
bool getLocalTimeInfo(struct tm *info) { //deprecated function
  return getLocalTime(info);
}
