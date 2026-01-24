// FirebaseManager.cpp  (was your firebase.cpp)
// Updated: daily meal notifications under /logs/meal_notifications/YYYY-MM-DD/{idx}
// Removed: /status/feedingStartEventId publisher

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include "WifiConnector.h"
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <WiFi.h>
#include "FirebaseManager.h"
#include <ArduinoJson.h>
#include <time.h>
#include <cstring>
#include <math.h>   // lroundf
#include "LocalManager.h"
#include "Secrets.h"

// Firebase

// Auth + Firebase objects
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;
bool didRead = false;

// ---------------- Schedule State ----------------
static FeedingScheduleEntry g_schedule[6];
static bool g_firedToday[6] = {false,false,false,false,false,false};
static int g_lastYDay = -1;

// per-slot signature so changing a slot resets only that slot’s fired flag
static uint32_t g_slotSig[6] = {0,0,0,0,0,0};

static uint32_t fnv1a32(const char* s) {//helper function for parsing the schedule
  uint32_t h = 2166136261u;
  if (!s) return h;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

static uint32_t computeScheduleSig(const FeedingScheduleEntry& e) { //helper function for parsing the schedule
  if (!e.enabled) return 0;

  uint32_t h = 2166136261u;
  h ^= (uint32_t)e.hour;        h *= 16777619u;
  h ^= (uint32_t)e.minute;      h *= 16777619u;
  h ^= (uint32_t)e.amountGrams; h *= 16777619u;
  h ^= fnv1a32(e.mealName);     h *= 16777619u;

  return h;
}

// Forward declaration (used in firebaseLoop before definition)
static void fetchScheduleFromRTDB_V2();

void firebaseCB(AsyncResult &aResult) { //deprecated function, we dont use it
  if (!aResult.isResult()) return;

  if (aResult.isEvent()) {
    Serial.printf("Event [%s]: %s (%d)\n",
                  aResult.uid().c_str(),
                  aResult.appEvent().message().c_str(),
                  aResult.appEvent().code());
  }

  if (aResult.isError()) {
    Serial.printf(" Error [%s]: %s (code %d)\n",
                  aResult.uid().c_str(),
                  aResult.error().message().c_str(),
                  aResult.error().code());
  }

  if (aResult.available()) {
    Serial.printf(" Data [%s]: %s\n", aResult.uid().c_str(), aResult.c_str());
  }
}

// Parse time string (ISO or HH:MM[:SS])
static bool parseHourMinute(const char* s, int &hourOut, int &minuteOut) { // helper function for parsing meals
  if (!s || !*s) return false;

  const char* t = strchr(s, 'T');
  const char* p = (t) ? (t + 1) : s;

  if (!(p[0] && p[1] && p[2] == ':' && p[3] && p[4])) return false;

  hourOut = (p[0]-'0')*10 + (p[1]-'0');
  minuteOut = (p[3]-'0')*10 + (p[4]-'0');

  if (hourOut < 0 || hourOut > 23 || minuteOut < 0 || minuteOut > 59) return false;
  return true;
}

// Reset the "fired" flags once per new day
static void resetDailyFiredIfNeeded() { // helps to make sure we only deploy each feeding once
  time_t now = time(nullptr);
  if (now < 100000) return;

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  if (g_lastYDay == -1) {
    g_lastYDay = tmNow.tm_yday;
    return;
  }

  if (tmNow.tm_yday != g_lastYDay) {
    g_lastYDay = tmNow.tm_yday;
    for (int i = 0; i < 6; i++) g_firedToday[i] = false;
  }
}

void initFirebase() { //initialize connection to firebase
  ssl_client.setInsecure();

  initializeApp(aClient, app, getAuth(user_auth), firebaseCB, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  for (int i = 0; i < 6; i++) {
    g_schedule[i].enabled = false;
    g_schedule[i].hour = 0;
    g_schedule[i].minute = 0;
    g_schedule[i].amountGrams = 0;
    g_schedule[i].mealName[0] = '\0';

    g_firedToday[i] = false;
    g_slotSig[i] = 0;
  }

  Serial.println("Firebase init done, waiting for app.ready()...");
}

// (Legacy) Fetch "/feedings" as one JSON string and update g_schedule[].
static void fetchScheduleFromRTDB() { // deprecated function, we dont use it
  String json = Database.get<String>(aClient, "/feedings");

  if (json.length() == 0 || json == "null") {
    Serial.println("No feedings found at /feedings");
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("deserializeJson failed: ");
    Serial.println(err.c_str());
    return;
  }

  for (int i = 0; i < 6; i++) {
    g_schedule[i].enabled = false;
    g_schedule[i].hour = 0;
    g_schedule[i].minute = 0;
    g_schedule[i].amountGrams = 0;
    g_schedule[i].mealName[0] = '\0';
  }

  if (doc.is<JsonObject>()) {
    JsonObject root = doc.as<JsonObject>();

    for (int i = 0; i < 6; i++) {
      String key = String(i);
      if (!root.containsKey(key)) continue;

      JsonObject feeding = root[key].as<JsonObject>();

      const char* timeStr = feeding["timeOfFeeding"]   | "";
      int amount          = feeding["amountOfFeeding"] | 0;

      int hh = 0, mm = 0;
      if (!parseHourMinute(timeStr, hh, mm)) continue;
      if (amount <= 0) continue;

      const char* mealStr = feeding["meal_name"] | "";

      g_schedule[i].enabled = true;
      g_schedule[i].hour = hh;
      g_schedule[i].minute = mm;
      g_schedule[i].amountGrams = amount;
      strncpy(g_schedule[i].mealName, mealStr, sizeof(g_schedule[i].mealName) - 1);
      g_schedule[i].mealName[sizeof(g_schedule[i].mealName) - 1] = '\0';
    }
  } else if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < (int)arr.size() && i < 6; i++) {
      JsonObject feeding = arr[i].as<JsonObject>();

      const char* timeStr = feeding["timeOfFeeding"]   | "";
      int amount          = feeding["amountOfFeeding"] | 0;

      int hh = 0, mm = 0;
      if (!parseHourMinute(timeStr, hh, mm)) continue;
      if (amount <= 0) continue;

      const char* mealStr = feeding["meal_name"] | "";

      g_schedule[i].enabled = true;
      g_schedule[i].hour = hh;
      g_schedule[i].minute = mm;
      g_schedule[i].amountGrams = amount;
      strncpy(g_schedule[i].mealName, mealStr, sizeof(g_schedule[i].mealName) - 1);
      g_schedule[i].mealName[sizeof(g_schedule[i].mealName) - 1] = '\0';
    }
  }

  Serial.println(" Schedule updated from RTDB:");
  for (int i = 0; i < 6; i++) {
    if (!g_schedule[i].enabled) {
      Serial.printf("#%d: (disabled)\n", i);
      continue;
    }
    Serial.printf("#%d: %02d:%02d amount=%d\n",
                  i,
                  g_schedule[i].hour,
                  g_schedule[i].minute,
                  g_schedule[i].amountGrams);
  }
}

void firebaseLoop() { // make sure we dont pull the entire schedule too soon
  app.loop();

  resetDailyFiredIfNeeded();
  if (!app.ready()) return;

  static unsigned long lastFetchMs = 0;
  const unsigned long FETCH_INTERVAL_MS = 15UL * 1000UL;

  if (lastFetchMs == 0 || (millis() - lastFetchMs) >= FETCH_INTERVAL_MS) {
    lastFetchMs = millis();
    fetchScheduleFromRTDB_V2();
  }
}

bool firebaseGetDueFeeding(int &amountOut, int &feed_hour, int &feed_minute,
                           char *mealNameOut, size_t mealNameOutSize) { // check if now is feeding time
  amountOut = 0;
  feed_hour = 0;
  feed_minute = 0;
  if (mealNameOut && mealNameOutSize > 0) {
    mealNameOut[0] = '\0';
  }

  time_t now = time(nullptr);
  if (now < 100000) return false;

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  for (int i = 0; i < 6; i++) {
    if (!g_schedule[i].enabled) continue;
    if (g_firedToday[i]) continue;

    if (tmNow.tm_hour == g_schedule[i].hour &&
        tmNow.tm_min  == g_schedule[i].minute) {

      g_firedToday[i] = true;
      amountOut = g_schedule[i].amountGrams;
      feed_hour = g_schedule[i].hour;
      feed_minute = g_schedule[i].minute;

      if (mealNameOut && mealNameOutSize > 0) {
        strncpy(mealNameOut, g_schedule[i].mealName, mealNameOutSize - 1);
        mealNameOut[mealNameOutSize - 1] = '\0';
      }

      return true;
    }
  }

  return false;
}

// New parser for DB schema:
// /feedings/{0..5}/hour = "HH:MM"
// /feedings/{0..5}/amount_grams = int
// /feedings/{0..5}/meal_name = string (optional)
static void fetchScheduleFromRTDB_V2() { // fetch schedule from firebasae and parse it
  const char* PATH = "/feedings";

  String json = Database.get<String>(aClient, PATH);

  if (json.length() == 0 || json == "null") {
    Serial.printf("No feedings found at %s\n", PATH);
    return;
  }

  // cache offline
  localStoreScheduleIfChanged(json.c_str());

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("V2 deserializeJson failed: ");
    Serial.println(err.c_str());
    return;
  }

  for (int i = 0; i < 6; i++) {
    g_schedule[i].enabled = false;
    g_schedule[i].hour = 0;
    g_schedule[i].minute = 0;
    g_schedule[i].amountGrams = 0;
    g_schedule[i].mealName[0] = '\0';
  }

  auto loadOne = [&](JsonObject feeding, int slot) {
    const char* hourStr = feeding["hour"] | "";
    int grams           = feeding["amount_grams"] | 0;
    const char* mealStr = feeding["meal_name"] | "";

    int hh = 0, mm = 0;
    if (!parseHourMinute(hourStr, hh, mm)) return;
    if (grams <= 0) return;

    g_schedule[slot].enabled = true;
    g_schedule[slot].hour = hh;
    g_schedule[slot].minute = mm;
    g_schedule[slot].amountGrams = grams;

    strncpy(g_schedule[slot].mealName, mealStr, sizeof(g_schedule[slot].mealName) - 1);
    g_schedule[slot].mealName[sizeof(g_schedule[slot].mealName) - 1] = '\0';
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
    Serial.println("V2 schedule format error: expected JSON object or array under /feedings");
    return;
  }

  Serial.println(" Schedule updated from RTDB (V2 schema):");
  for (int i = 0; i < 6; i++) {
    if (!g_schedule[i].enabled) {
      Serial.printf("#%d: (disabled)\n", i);
    } else {
      if (g_schedule[i].mealName[0]) {
        Serial.printf("#%d: %02d:%02d grams=%d meal=%s\n",
                      i, g_schedule[i].hour, g_schedule[i].minute,
                      g_schedule[i].amountGrams, g_schedule[i].mealName);
      } else {
        Serial.printf("#%d: %02d:%02d grams=%d\n",
                      i, g_schedule[i].hour, g_schedule[i].minute,
                      g_schedule[i].amountGrams);
      }
    }
  }

  // reset fired flag ONLY if that slot’s schedule changed
  for (int i = 0; i < 6; i++) {
    uint32_t newSig = computeScheduleSig(g_schedule[i]);

    if (newSig != g_slotSig[i]) {
      g_firedToday[i] = false;
      g_slotSig[i] = newSig;
      Serial.printf("[DBG] slot %d changed -> reset firedToday\n", i);
    }

    if (!g_schedule[i].enabled) {
      g_firedToday[i] = false;
      g_slotSig[i] = 0;
    }
  }
}

void firebaseSetContainerEmpty(bool empty) { // container is empty helper function
  app.loop();
  if (!app.ready()) return;

  Database.set(aClient, "/deviceState/containerEmpty", empty);
}

// Finds next numeric index under /weights (0,1,2,...)
static int getNextWeightIndex() { // helper to update weight, make sure that in db the meals have unique index number
  const char *PATH = "/weights";

  String json = Database.get<String>(aClient, PATH);
  if (json.length() == 0 || json == "null") {
    return 0;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("getNextWeightIndex deserializeJson failed: ");
    Serial.println(err.c_str());
    return 0;
  }

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    return (int)arr.size();
  }

  if (doc.is<JsonObject>()) {
    JsonObject obj = doc.as<JsonObject>();
    int maxIdx = -1;

    for (JsonPair kv : obj) {
      const char *k = kv.key().c_str();
      int idx = atoi(k);
      if (idx > maxIdx) maxIdx = idx;
    }
    return maxIdx + 1;
  }

  return 0;
}

bool update_weight(int amount_grams,
                   int feed_hour,
                   int feed_minute,
                   const char *meal_name,
                   const char *day,
                   const char *date,
                   float prev_current_weight,
                   float new_current_weight) { //upload meal to statistics
  app.loop();
  if (!app.ready()) return false;

  char hourStr[6];
  snprintf(hourStr, sizeof(hourStr), "%02d:%02d", feed_hour, feed_minute);

  int prevWeightInt = (int)lroundf(prev_current_weight);
  int newWeightInt  = (int)lroundf(new_current_weight);

  int idx = getNextWeightIndex();
  String base = String("/weights/") + String(idx);

  bool ok = true;

  ok &= Database.set(aClient, base + "/amount_grams", amount_grams);
  ok &= Database.set(aClient, base + "/prev_current_weight", prevWeightInt);
  ok &= Database.set(aClient, base + "/new_current_weight", newWeightInt);
  ok &= Database.set(aClient, base + "/day", String(day ? day : ""));
  ok &= Database.set(aClient, base + "/date", String(date ? date : ""));
  ok &= Database.set(aClient, base + "/hour", String(hourStr));
  ok &= Database.set(aClient, base + "/meal_name", String(meal_name ? meal_name : ""));

  if (ok) {
    Serial.printf(" update_weight uploaded to %s\n", base.c_str());
  } else {
    Serial.printf("update_weight failed to upload to %s\n", base.c_str());
  }

  return ok;
}

// ---------------- Container Status (RTDB) ----------------
static const char* kContainerStatusPath = "/status/container";

static int32_t makeEventIdEpochSeconds() { // generate unique id base of the time
  time_t now = time(nullptr);
  if (now >= 100000) return (int32_t)now;
  return (int32_t)(millis() / 1000UL);
}

static void printLastFirebaseError(const char* ctx) { //debugging purposes
  int code = aClient.lastError().code();
  const String msg = aClient.lastError().message();
  Serial.printf(" %s failed: code=%d msg=%s\n", ctx, code, msg.c_str());
}

bool firebasePublishContainerEmpty(bool emptyNow) { //upload container is empty notification to firebase
  Serial.printf("[DBG] entered firebasePublishContainerEmpty empty=%d\n", emptyNow);

  app.loop();
  if (!app.ready()) {
    Serial.println(" firebasePublishContainerEmpty: app not ready yet (will retry)");
    return false;
  }

  const int32_t nowSec = makeEventIdEpochSeconds();

  bool ok = Database.set<bool>(aClient, String(kContainerStatusPath) + "/empty", emptyNow);
  if (!ok) {
    printLastFirebaseError("RTDB set /status/container/empty");
    return false;
  }

  if (emptyNow) {
    ok = Database.set<int>(aClient, String(kContainerStatusPath) + "/eventId", (int)nowSec);
    if (!ok) {
      printLastFirebaseError("RTDB set /status/container/eventId");
      return false;
    }

    ok = Database.set<int>(aClient, String(kContainerStatusPath) + "/emptySince", (int)nowSec);
    if (!ok) {
      printLastFirebaseError("RTDB set /status/container/emptySince");
      return false;
    }
  } else {
    ok = Database.set<int>(aClient, String(kContainerStatusPath) + "/clearedAt", (int)nowSec);
    if (!ok) {
      printLastFirebaseError("RTDB set /status/container/clearedAt");
      // still return true because /empty already succeeded
    }
  }

  Serial.printf("Published container status to RTDB: empty=%s\n", emptyNow ? "true" : "false");
  return true;
}

// ---------------- Daily Meal Notifications (RTDB) ----------------
// /logs/meal_notifications/YYYY-MM-DD/{idx}

static bool getCurrentDateISO(char* out, size_t outSize) {// log meal helper function
  if (!out || outSize < 11) return false;

  time_t now = time(nullptr);
  if (now < 100000) {
    out[0] = '\0';
    return false;
  }

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  snprintf(out, outSize, "%04d-%02d-%02d",
           tmNow.tm_year + 1900,
           tmNow.tm_mon + 1,
           tmNow.tm_mday);
  return true;
}

static int getNextMealNotificationIndex(const String& basePath) { // log meal helper function
  String json = Database.get<String>(aClient, basePath);

  if (json.length() == 0 || json == "null") return 0;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("getNextMealNotificationIndex deserializeJson failed: ");
    Serial.println(err.c_str());
    return 0;
  }

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    return (int)arr.size();
  }

  if (doc.is<JsonObject>()) {
    JsonObject obj = doc.as<JsonObject>();
    int maxIdx = -1;

    for (JsonPair kv : obj) {
      int idx = atoi(kv.key().c_str());
      if (idx > maxIdx) maxIdx = idx;
    }
    return maxIdx + 1;
  }

  return 0;
}

bool firebaseLogMealNotification(const char* type,
                                 const char* mealName,
                                 int hour,
                                 int minute,
                                 int amountGrams,
                                 int32_t eventId) { // upload to meal data for statistics
  app.loop();
  if (!app.ready()) {
    Serial.println(" firebaseLogMealNotification: app not ready yet");
    return false;
  }

  char dateISO[11];
  if (!getCurrentDateISO(dateISO, sizeof(dateISO))) {
    Serial.println(" firebaseLogMealNotification: time not ready (no date) -> skip");
    return false;
  }

  const int32_t nowSec = makeEventIdEpochSeconds();

  String dayPath = String("/logs/meal_notifications/") + String(dateISO);
  int idx = getNextMealNotificationIndex(dayPath);
  String base = dayPath + "/" + String(idx);

  bool ok = true;
  ok &= Database.set<int>(aClient, base + "/ts", (int)nowSec);
  ok &= Database.set<String>(aClient, base + "/type", String(type ? type : ""));
  ok &= Database.set<String>(aClient, base + "/meal_name", String(mealName ? mealName : ""));
  ok &= Database.set<int>(aClient, base + "/hour", hour);
  ok &= Database.set<int>(aClient, base + "/minute", minute);
  ok &= Database.set<int>(aClient, base + "/amount_grams", amountGrams);
  ok &= Database.set<int>(aClient, base + "/eventId", (int)eventId);

  if (!ok) {
    printLastFirebaseError("RTDB set daily /logs/meal_notifications/<date>");
    Serial.println("Meal notification log failed");
    return false;
  }

  Serial.printf("Logged DAILY meal notification: date=%s type=%s meal=%s %02d:%02d grams=%d eventId=%d\n",
                dateISO,
                type ? type : "",
                mealName ? mealName : "",
                hour, minute,
                amountGrams,
                (int)eventId);
  return true;
}

// ---------------- Connectivity (Offline mode) ----------------
bool firebaseIsDatabaseConnected() { //check if we are connected to firebase
  app.loop();

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  return true; // the rest of the function doesnt work properly

  // (your old probe logic below is intentionally left unreachable, as in your original file)
  static unsigned long lastProbeMs = 0;
  bool lastResult = true;
  const unsigned long PROBE_INTERVAL_MS = 5000;

  if (lastProbeMs != 0 && (millis() - lastProbeMs) < PROBE_INTERVAL_MS) {
    return lastResult;
  }
  lastProbeMs = millis();

  bool infoConnected = Database.get<bool>(aClient, "/.info/connected");
  int code = aClient.lastError().code();
  lastResult = (code == 0);
  if(!lastResult){
  }
  return lastResult;
}
