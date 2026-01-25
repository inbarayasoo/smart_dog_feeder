#include "LocalManager.h"
#include <LittleFS.h>
#include <Arduino.h>  
#include <FS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cstring>

static const char* SCHEDULE_FILE = "/schedule_cache.json";
static const char* SCHEDULE_CRC_FILE = "/schedule_cache.crc";

// -------------------- Offline stats queue (weights) --------------------
static const char* WEIGHTS_QUEUE_FILE = "/weights_queue.jsonl";
static const char* WEIGHTS_PRUNE_META = "/weights_prune_meta.json";

static const uint32_t PRUNE_INTERVAL_SEC = 24UL * 60UL * 60UL;        // 24 hours
static const uint32_t KEEP_WINDOW_SEC    = 7UL  * 24UL * 60UL * 60UL; // 7 days

// Simple CRC32 (good enough for change-detection)
static uint32_t crc32(const uint8_t* data, size_t len) { 
  uint32_t crc = 0xFFFFFFFF;
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++) {
      uint32_t mask = -(crc & 1);
      crc = (crc >> 1) ^ (0xEDB88320 & mask);
    }
  }
  return ~crc;
}

// initiallize local storage
bool initLocalStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("[Local] LittleFS mount failed");
    return false;
  }
  Serial.println("[Local] LittleFS mounted");
  return true;
}

//helper function for reading local schedule file
static bool readStoredCrc(uint32_t &out) {
  if (!LittleFS.exists(SCHEDULE_CRC_FILE)) return false;
  File f = LittleFS.open(SCHEDULE_CRC_FILE, "r");
  if (!f) return false;
  String s = f.readString();
  f.close();
  out = (uint32_t)strtoul(s.c_str(), nullptr, 10);
  return true;
}
//helper function for reading local schedule file
static bool writeStoredCrc(uint32_t crc) {
  File f = LittleFS.open(SCHEDULE_CRC_FILE, "w");
  if (!f) return false;
  f.print(crc);
  f.close();
  return true;
}

// check if we need to update the local storage
bool localStoreScheduleIfChanged(const char* json) {
  if (!json) return false;

  uint32_t newCrc = crc32((const uint8_t*)json, strlen(json));

  uint32_t oldCrc = 0;
  bool hasOld = readStoredCrc(oldCrc);

  if (hasOld && oldCrc == newCrc) {
    // No change -> no flash write
    return true;
  }

  File f = LittleFS.open(SCHEDULE_FILE, "w");
  if (!f) {
    Serial.println("[Local] Failed to open schedule file for write");
    return false;
  }
  f.print(json);
  f.close();

  if (!writeStoredCrc(newCrc)) {
    Serial.println("[Local] Failed to write schedule CRC");
    // schedule still saved, CRC missing is not fatal
  }

  Serial.println("[Local] Schedule saved/updated");
  return true;
}

//load the saved locally schedule
bool localLoadSchedule(String &outJson) {
  outJson = "";
  if (!LittleFS.exists(SCHEDULE_FILE)) return false;

  File f = LittleFS.open(SCHEDULE_FILE, "r");
  if (!f) return false;

  outJson = f.readString();
  f.close();

  return outJson.length() > 0;
}

// -------------------- Stats queue helpers --------------------

//deprecated function
bool localWeightsQueueExists() {
  return LittleFS.exists(WEIGHTS_QUEUE_FILE);
}

// helper function for checking time
static bool loadLastPruneTs(uint32_t &outTs) {
  outTs = 0;
  if (!LittleFS.exists(WEIGHTS_PRUNE_META)) return false;

  File f = LittleFS.open(WEIGHTS_PRUNE_META, "r");
  if (!f) return false;

  String s = f.readString();
  f.close();

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, s)) return false;

  outTs = doc["last_prune_ts"] | 0;
  return true;
}

// helper function for checking time
static bool saveLastPruneTs(uint32_t ts) {
  DynamicJsonDocument doc(256);
  doc["last_prune_ts"] = ts;

  File f = LittleFS.open(WEIGHTS_PRUNE_META, "w");
  if (!f) return false;

  serializeJson(doc, f);
  f.close();
  return true;
}

// helper function for checking time
static uint32_t getValidEpochOrZero() {
  time_t now = time(nullptr);
  if (now >= 100000) return (uint32_t)now;
  return 0; // 0 means "unknown time"
}

// we dont want to store meals in local storage forever, so we delete the older than a week ones
static bool pruneWeightsQueueIfDue() {
  uint32_t now = getValidEpochOrZero();
  if (now == 0) return true; // no valid time -> skip age-based pruning safely

  uint32_t lastPrune = 0;
  loadLastPruneTs(lastPrune);

  if (lastPrune != 0 && (now - lastPrune) < PRUNE_INTERVAL_SEC) {
    return true; // not time yet
  }

  if (!LittleFS.exists(WEIGHTS_QUEUE_FILE)) {
    (void)saveLastPruneTs(now);
    return true;
  }

  File in = LittleFS.open(WEIGHTS_QUEUE_FILE, "r");
  if (!in) return false;

  File out = LittleFS.open("/weights_queue.tmp", "w");
  if (!out) {
    in.close();
    return false;
  }

  const uint32_t cutoff = now - KEEP_WINDOW_SEC;

  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      // corrupted / partial line -> drop it
      continue;
    }

    uint32_t ts = doc["ts"] | 0;

    // keep if:
    // - ts is 0 (unknown time) OR
    // - ts is within last 7 days
    if (ts == 0 || ts >= cutoff) {
      out.print(line);
      out.print("\n");
    }
  }

  in.close();
  out.close();

  LittleFS.remove(WEIGHTS_QUEUE_FILE);
  LittleFS.rename("/weights_queue.tmp", WEIGHTS_QUEUE_FILE);

  (void)saveLastPruneTs(now);
  Serial.println("[Local] Weights queue pruned (kept last 7 days, checked every 24h)");
  return true;
}

// add offline mode records here
bool localQueueWeightUpdate(int dueAmount,
                            int feed_hour,
                            int feed_minute,
                            const char* mealName,
                            const char* day,
                            const char* dateISO,
                            float prevWeight,
                            float currentWeight) {
  // prune (at most once every 24h, only if time is valid)
  (void)pruneWeightsQueueIfDue();

  File f = LittleFS.open(WEIGHTS_QUEUE_FILE, "a");
  if (!f) {
    Serial.println("[Local] Failed to open weights queue for append");
    return false;
  }

  DynamicJsonDocument doc(512);
  doc["type"] = "weight_update";
  doc["ts"]   = getValidEpochOrZero(); // 0 if time invalid (safe)

  doc["dueAmount"]   = dueAmount;
  doc["feed_hour"]   = feed_hour;
  doc["feed_minute"] = feed_minute;

  doc["mealName"] = mealName ? mealName : "";
  doc["day"]      = day ? day : "";
  doc["dateISO"]  = dateISO ? dateISO : "";

  doc["prevWeight"]    = prevWeight;
  doc["currentWeight"] = currentWeight;

  serializeJson(doc, f);
  f.print("\n");
  f.close();

  Serial.println("[Local] Queued weight update locally");
  return true;
}

// Flush queue to Firebase with safety:
// - If upload fails on some line, keep that line + the remaining lines (so no duplicates)
bool localFlushWeightsQueue(WeightUploadFn uploadFn) {
  if (!uploadFn) return false;

  if (!LittleFS.exists(WEIGHTS_QUEUE_FILE)) {
    return true; // nothing to do
  }

  // optional prune before flush (still respects 24h rule)
  (void)pruneWeightsQueueIfDue();

  File in = LittleFS.open(WEIGHTS_QUEUE_FILE, "r");
  if (!in) return false;

  File out = LittleFS.open("/weights_queue.rem", "w"); // remaining lines if fail
  if (!out) {
    in.close();
    return false;
  }

  bool failed = false;

  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      // drop corrupted line
      continue;
    }

    // Parse fields (match what we stored)
    int dueAmount = doc["dueAmount"] | 0;
    int hh        = doc["feed_hour"] | 0;
    int mm        = doc["feed_minute"] | 0;

    const char* meal = doc["mealName"] | "";
    const char* day  = doc["day"]      | "";
    const char* date = doc["dateISO"]  | "";

    float prevW = doc["prevWeight"]    | 0.0f;
    float currW = doc["currentWeight"] | 0.0f;

    bool ok = uploadFn(dueAmount, hh, mm, meal, day, date, prevW, currW);
    if (!ok) {
      // Keep this line + everything after it
      failed = true;
      out.print(line);
      out.print("\n");

      while (in.available()) {
        String rest = in.readStringUntil('\n');
        rest.trim();
        if (rest.length() == 0) continue;
        out.print(rest);
        out.print("\n");
      }
      break;
    }
  }

  in.close();
  out.close();

  if (!failed) {
    // All uploaded -> delete queue + cleanup remainder file
    LittleFS.remove(WEIGHTS_QUEUE_FILE);
    LittleFS.remove("/weights_queue.rem");
    Serial.println("[Local]  Queue fully uploaded -> deleted local queue file");
    return true;
  }

  // Failure -> replace queue with remaining lines only
  LittleFS.remove(WEIGHTS_QUEUE_FILE);
  LittleFS.rename("/weights_queue.rem", WEIGHTS_QUEUE_FILE);
  Serial.println("[Local]  Queue upload partial -> kept remaining lines for retry");
  return false;
}

// -------------------- Phase 3: Offline schedule execution --------------------

// Local schedule state (mirrors FirebaseManager behavior)
struct LocalScheduleEntry {
  bool enabled;
  int hour;
  int minute;
  int amountGrams;
  char mealName[24];
};

static LocalScheduleEntry g_localSchedule[6];
static bool g_localFiredToday[6] = {false,false,false,false,false,false};
static int g_localLastYDay = -1;

// For detecting per-slot changes (so we can reset fired flag only for changed slots)
static uint32_t g_localSlotSig[6] = {0,0,0,0,0,0};

// Cache parsed JSON in RAM (so we don’t parse on every loop)
static uint32_t g_lastScheduleCrcRam = 0;
static bool g_haveCrcRam = false;
static unsigned long g_lastParseMs = 0;
static const unsigned long LOCAL_PARSE_THROTTLE_MS = 5000; // don’t parse more than every 5s

static uint32_t fnv1a32_local(const char* s) {//helper function for parsing the schedule
  uint32_t h = 2166136261u;
  if (!s) return h;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

static uint32_t computeLocalSlotSig(const LocalScheduleEntry& e) {//helper function for parsing the schedule
  if (!e.enabled) return 0;
  uint32_t h = 2166136261u;
  h ^= (uint32_t)e.hour;        h *= 16777619u;
  h ^= (uint32_t)e.minute;      h *= 16777619u;
  h ^= (uint32_t)e.amountGrams; h *= 16777619u;
  h ^= fnv1a32_local(e.mealName); h *= 16777619u;
  return h;
}

// Parse "HH:MM" (or ISO string with 'T')
static bool parseHourMinuteLocal(const char* s, int &hourOut, int &minuteOut) { // feeding parser for offline
  if (!s || !*s) return false;
  const char* t = strchr(s, 'T');
  const char* p = (t) ? (t + 1) : s;

  if (!(p[0] && p[1] && p[2] == ':' && p[3] && p[4])) return false;

  hourOut = (p[0]-'0')*10 + (p[1]-'0');
  minuteOut = (p[3]-'0')*10 + (p[4]-'0');

  if (hourOut < 0 || hourOut > 23 || minuteOut < 0 || minuteOut > 59) return false;
  return true;
}

static void resetLocalDailyFiredIfNeeded() { // helper function to make sure we deploy each feeding only once
  time_t now = time(nullptr);
  if (now < 100000) return; // NTP not ready

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  if (g_localLastYDay == -1) {
    g_localLastYDay = tmNow.tm_yday;
    return;
  }

  if (tmNow.tm_yday != g_localLastYDay) {
    g_localLastYDay = tmNow.tm_yday;
    for (int i = 0; i < 6; i++) g_localFiredToday[i] = false;
  }
}

static void clearLocalSchedule() {
  for (int i = 0; i < 6; i++) {
    g_localSchedule[i].enabled = false;
    g_localSchedule[i].hour = 0;
    g_localSchedule[i].minute = 0;
    g_localSchedule[i].amountGrams = 0;
    g_localSchedule[i].mealName[0] = '\0';
  }
}

// Parse cached schedule JSON into g_localSchedule[] (V2 schema)
static bool parseLocalScheduleJson(const String &json) {
  if (json.length() == 0) return false;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("[Local] deserializeJson failed: ");
    Serial.println(err.c_str());
    return false;
  }

  clearLocalSchedule();

  auto loadOne = [&](JsonObject feeding, int slot) {
    const char* hourStr = feeding["hour"] | "";
    int grams           = feeding["amount_grams"] | 0;
    const char* mealStr = feeding["meal_name"] | "";

    int hh = 0, mm = 0;
    if (!parseHourMinuteLocal(hourStr, hh, mm)) return;
    if (grams <= 0) return;

    g_localSchedule[slot].enabled = true;
    g_localSchedule[slot].hour = hh;
    g_localSchedule[slot].minute = mm;
    g_localSchedule[slot].amountGrams = grams;

    strncpy(g_localSchedule[slot].mealName, mealStr,
            sizeof(g_localSchedule[slot].mealName) - 1);
    g_localSchedule[slot].mealName[sizeof(g_localSchedule[slot].mealName) - 1] = '\0';
  };

  if (doc.is<JsonObject>()) {
    JsonObject root = doc.as<JsonObject>();
    for (int i = 0; i < 6; i++) {
      String key = String(i);
      if (!root.containsKey(key)) continue;

      JsonObject feeding = root[key].as<JsonObject>();
      if (feeding.isNull()) continue;

      loadOne(feeding, i);
    }
  } else if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < (int)arr.size() && i < 6; i++) {
      JsonObject feeding = arr[i].as<JsonObject>();
      if (feeding.isNull()) continue;

      loadOne(feeding, i);
    }
  } else {
    Serial.println("[Local] schedule format error: expected JSON object or array");
    return false;
  }

  // Reset only changed slots’ fired flag (match Firebase behavior)
  for (int i = 0; i < 6; i++) {
    uint32_t newSig = computeLocalSlotSig(g_localSchedule[i]);
    if (newSig != g_localSlotSig[i]) {
      g_localFiredToday[i] = false;
      g_localSlotSig[i] = newSig;
      Serial.printf("[Local] slot %d changed -> reset firedToday\n", i);
    }
    if (!g_localSchedule[i].enabled) {
      g_localFiredToday[i] = false;
      g_localSlotSig[i] = 0;
    }
  }

  Serial.println("[Local]  Schedule loaded from cache:");
  for (int i = 0; i < 6; i++) {
    if (!g_localSchedule[i].enabled) {
      Serial.printf("[Local] #%d: (disabled)\n", i);
    } else {
      Serial.printf("[Local] #%d: %02d:%02d grams=%d meal=%s\n",
                    i,
                    g_localSchedule[i].hour,
                    g_localSchedule[i].minute,
                    g_localSchedule[i].amountGrams,
                    g_localSchedule[i].mealName);
    }
  }

  return true;
}

// Ensure local schedule is parsed (only re-parse if file content changed)
static bool ensureLocalScheduleUpToDate() { // make sure offline schedule is the same as the online schedule
  if (g_lastParseMs != 0 && (millis() - g_lastParseMs) < LOCAL_PARSE_THROTTLE_MS) {
    return true;
  }
  g_lastParseMs = millis();

  String json;
  if (!localLoadSchedule(json)) {
    Serial.println("[Local] No schedule cache file found");
    return false;
  }

  uint32_t crc = crc32((const uint8_t*)json.c_str(), json.length());
  if (g_haveCrcRam && crc == g_lastScheduleCrcRam) {
    return true;
  }

  bool ok = parseLocalScheduleJson(json);
  if (ok) {
    g_lastScheduleCrcRam = crc;
    g_haveCrcRam = true;
  }
  return ok;
}

bool localGetDueFeeding(int &amountOut,
                        int &feed_hour,
                        int &feed_minute,
                        char *mealNameOut,
                        size_t mealNameOutSize) { // check if now is the time to feed, offline version
  amountOut = 0;
  feed_hour = 0;
  feed_minute = 0;
  if (mealNameOut && mealNameOutSize > 0) mealNameOut[0] = '\0';

  resetLocalDailyFiredIfNeeded();

  time_t now = time(nullptr);
  if (now < 100000) {
    return false;
  }

  if (!ensureLocalScheduleUpToDate()) {
    return false;
  }

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  for (int i = 0; i < 6; i++) {
    if (!g_localSchedule[i].enabled) continue;
    if (g_localFiredToday[i]) continue;

    if (tmNow.tm_hour == g_localSchedule[i].hour &&
        tmNow.tm_min  == g_localSchedule[i].minute) {

      g_localFiredToday[i] = true;

      amountOut = g_localSchedule[i].amountGrams;
      feed_hour = g_localSchedule[i].hour;
      feed_minute = g_localSchedule[i].minute;

      if (mealNameOut && mealNameOutSize > 0) {
        strncpy(mealNameOut, g_localSchedule[i].mealName, mealNameOutSize - 1);
        mealNameOut[mealNameOutSize - 1] = '\0';
      }

      return true;
    }
  }

  return false;
}
