
#include "DistanceManager.h"
#include "MotorManager.h"
#include "PixelManager.h"
#include "ScaleManager.h"
#include "WifiConnector.h"
#include "NtpManager.h"
#include "FirebaseManager.h"
#include "LocalManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <stdio.h>
#include <cstring>
#include <time.h>
#include <stdint.h>   // for int32_t
#include <math.h>     // for lroundf

// -------------------- Definitions ------------------------------------

// Distance configs
unsigned long lastMeasureMillis = 0;
bool containerEmpty = false;     // true when the food container is empty
bool prevEmpty = false;

// Feed button
#define FEED_BUTTON_PIN 15   // Button between GPIO 15 and GND (INPUT_PULLUP)

// For edge detection on button (short press)
bool prevButtonPressed = false;

// Weight configs
const float FEED_PORTION_GRAMS = 7.0f;

// Scheduled feeding request (set by schedule, consumed by state machine)
volatile bool scheduledFeedRequest = false;
float scheduledPortionGrams = 0.0f;

// To avoid stopping due to a single noisy spike
int aboveTargetCount = 0;
const int REQUIRED_ABOVE_TARGET = 2;

// Safety timeout for feeding
const unsigned long FEED_TIMEOUT_MS = 30000;
unsigned long feedStartMillis = 0;

// Weight variables
float currentWeightGramsRecieved = 0.0f;
float feedTargetWeightGrams      = 0.0f;   // Target = current weight + portion grams

// --------- RTDB container status publish (retry) ----------
bool pendingContainerStatusUpdate = false;
bool pendingContainerEmptyValue = false;
unsigned long lastContainerStatusPublishAttemptMs = 0;
const unsigned long CONTAINER_STATUS_PUBLISH_RETRY_MS = 5000; // 5s

// Feeding notification logging (RTDB)
bool firebaseLogMealNotification(const char* type,
                                 const char* mealName,
                                 int hour,
                                 int minute,
                                 int amountGrams,
                                 int32_t eventId);

// Track current feeding "session"
int32_t currentFeedingEventId = -1;


static bool feedingStopNotified = false;

// One-time initial sync after boot
bool didInitialContainerSync = false;

// ---- Container empty debouncer ----
bool rawEmptyCandidate = false;
unsigned long rawEmptySinceMs = 0;
const unsigned long EMPTY_STABLE_MS = 800;

// ---------- Motor state based on container ----------
enum MotorState {
  MOTOR_ENABLED,
  MOTOR_DISABLED
};
MotorState motorState = MOTOR_ENABLED;

// ---------- Feeding state machine ----------
enum FeedState {
  FEED_IDLE,
  FEED_ACTIVE
};
FeedState feedState = FEED_IDLE;

// Track if we already started motor for this feeding cycle
bool motorStartedThisCycle = false;

// Throttle serial prints
static unsigned long lastWeightPrintMs = 0;

// Check schedule due
int dueAmount = 0;
int feed_hour = 0;
int feed_minute = 0;
char mealName[30] = {0};
char day[10] = {0};
char dateISO[11] = {0};        // "YYYY-MM-DD"

// prev feeding
int prev_dueAmount = 0;
int prev_feed_hour = 0;
int prev_feed_minute = 0;
char prev_mealName[30] = {0};
char prev_day[10] = {0};
char prev_dateISO[11] = {0};
float prev_currentWeightGramsRecieved = 0.0f;

bool upload_status = true;

// ---------- Timeout recovery wiggle ----------
enum TimeoutRecoveryPhase {
  TR_NONE,
  TR_BACKWARD,
  TR_FORWARD
};
TimeoutRecoveryPhase timeoutRecoveryPhase = TR_NONE;
unsigned long timeoutRecoveryPhaseStartMs = 0;

const unsigned long RECOVERY_STEPS_PER_REV_EFFECTIVE = 800;
const unsigned long RECOVERY_SPEED_STEPS_PER_SEC     = 350;

const unsigned long TIMEOUT_RECOVER_BACK_MS =
    (unsigned long)((RECOVERY_STEPS_PER_REV_EFFECTIVE * 1000UL) / RECOVERY_SPEED_STEPS_PER_SEC);

const unsigned long TIMEOUT_RECOVER_FWD_MS  =
    (unsigned long)((RECOVERY_STEPS_PER_REV_EFFECTIVE * 1000UL) / RECOVERY_SPEED_STEPS_PER_SEC);

int timeoutRecoveryCount = 0;
const int TIMEOUT_RECOVERY_MAX = 1;
float weightAtTimeout = 0.0f;

// ---- Final weight capture after stop ----
bool pendingFinalWeight = false;
unsigned long motorStoppedAtMs = 0;
const unsigned long FINAL_WEIGHT_SETTLE_MS = 700; // tweak 300–1500


static unsigned long pendingFinalWeightSinceMs = 0;
static const unsigned long FINAL_WEIGHT_MAX_WAIT_MS = 6000; // 6s (tweak 3000–15000)

// ---- Offline queue flush throttle ----
static unsigned long lastQueueSyncMs = 0;
const unsigned long QUEUE_SYNC_INTERVAL_MS = 10000; // 10s

// ---- Duplicate scheduled feeding suppression (same minute reconnect) ----
static int  lastSchedYday   = -1;
static int  lastSchedHour   = -1;
static int  lastSchedMinute = -1;
static char lastSchedMeal[24] = {0};

static bool isDuplicateScheduledMinute(const char* meal, int hh, int mm) { //make sure we dont deploy the same meal more than once
  time_t now = time(nullptr);
  if (now < 100000) return false; // if time unknown, don't suppress

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  const char* m = meal ? meal : "";

  if (tmNow.tm_yday == lastSchedYday &&
      hh == lastSchedHour &&
      mm == lastSchedMinute &&
      strncmp(m, lastSchedMeal, sizeof(lastSchedMeal)) == 0) {
    return true;
  }

  lastSchedYday   = tmNow.tm_yday;
  lastSchedHour   = hh;
  lastSchedMinute = mm;

  strncpy(lastSchedMeal, m, sizeof(lastSchedMeal) - 1);
  lastSchedMeal[sizeof(lastSchedMeal) - 1] = '\0';

  return false;
}

// -------------------- Offline interval feeding policy --------------------
// (Currently short values for testing)
static const uint32_t OFFLINE_INTERVAL_MS      = 60UL * 1000UL;    // 30 seconds (testing)
static const uint32_t OFFLINE_REBOOT_SAFETY_MS = 10UL * 1000UL;  // 60 seconds (testing)
static uint32_t nextOfflineFeedMs = 0;

// -------------------- AUTO portal policy --------------------
static unsigned long offlineSinceMs = 0;
static const unsigned long OPEN_PORTAL_AFTER_MS = 100UL * 1000UL; // 10 seconds (testing)


static bool portalPending = false;
static unsigned long motorDoneSinceMs = 0;
static const unsigned long MOTOR_DONE_STABLE_MS = 400; // tweak 200–1000

// Firebase init tracking
static bool ntpValid = false;
static bool firebaseInited = false;

// Preferences (persist across reboot)
static Preferences prefs;
static uint32_t bootCounter = 0;

// --------------------  No-clock OFFLINE accumulator (RAM only) --------------------
static bool  noClockAccumHasData       = false;
static int   noClockAccumTotalGrams    = 0;
static float noClockAccumSumPrevWeight = 0.0f;
static float noClockAccumLastNewWeight = 0.0f;

// Track current feeding start info (so we can accumulate at END of feeding)
static bool  curFeedingNoClock       = false;
static int   curFeedingAmountGrams   = 0;
static float curFeedingStartWeight   = 0.0f;

// -------------------- Helpers --------------------
static bool timeIsValid() {
  time_t now = time(nullptr);
  return (now >= 100000);
}

static void prefsBootInitAndLoad() {
  prefs.begin("offline", false);
  bootCounter = prefs.getUInt("bootCounter", 0);
  bootCounter++;
  prefs.putUInt("bootCounter", bootCounter);
}

static uint32_t prefsGetLastOfflineFeedBoot() {
  return prefs.getUInt("lastOffBoot", 0);
}

static void prefsSetLastOfflineFeedBoot(uint32_t bootId) {
  prefs.putUInt("lastOffBoot", bootId);
}

static void prefsClearOfflineFeedMarker() {
  prefs.putUInt("lastOffBoot", 0);
}

static void scheduleInitialOfflineFeed() {
  const uint32_t lastOffBoot = prefsGetLastOfflineFeedBoot();
  const bool rebootSinceLastOfflineFeed = (lastOffBoot != 0 && lastOffBoot != bootCounter);

  if (!timeIsValid() && rebootSinceLastOfflineFeed) {
    nextOfflineFeedMs = millis() + OFFLINE_REBOOT_SAFETY_MS;
    Serial.println(" Offline safety: reboot since last offline feed -> delaying auto feed (safety window).");
  } else {
    nextOfflineFeedMs = millis() + OFFLINE_INTERVAL_MS;
  }
}

static int32_t makeEventIdEpochSecondsLocal() {//make a unique id from the time
  time_t now = time(nullptr);
  if (now >= 100000) return (int32_t)now;
  return (int32_t)(millis() / 1000UL);
}

void getCurrentDayNameNow(char *dayOut, size_t dayOutSize) {//print current time
  if (!dayOut || dayOutSize == 0) return;

  time_t now = time(nullptr);
  if (now < 100000) {
    strncpy(dayOut, "unknown", dayOutSize - 1);
    dayOut[dayOutSize - 1] = '\0';
    return;
  }

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  static const char *names[] = {
    "sunday", "monday", "tuesday", "wednesday",
    "thursday", "friday", "saturday"
  };

  const char *name = names[tmNow.tm_wday];
  strncpy(dayOut, name, dayOutSize - 1);
  dayOut[dayOutSize - 1] = '\0';
}

void getCurrentDateISONow(char *dateOut, size_t dateOutSize) { //print current time
  if (!dateOut || dateOutSize == 0) return;

  time_t now = time(nullptr);
  if (now < 100000) {
    strncpy(dateOut, "unknown", dateOutSize - 1);
    dateOut[dateOutSize - 1] = '\0';
    return;
  }

  struct tm tmNow;
  localtime_r(&now, &tmNow);

  snprintf(dateOut, dateOutSize, "%04d-%02d-%02d",
           tmNow.tm_year + 1900,
           tmNow.tm_mon + 1,
           tmNow.tm_mday);
}

void setCurrentDayName(char *dayOut, size_t dayOutSize) { getCurrentDayNameNow(dayOut, dayOutSize); }
void setCurrentDateISO(char *dateOut, size_t dateOutSize) { getCurrentDateISONow(dateOut, dateOutSize); }

void update_prevs() { //store latest feeding starting parameters
  prev_dueAmount  = dueAmount;
  prev_feed_hour  = feed_hour;
  prev_feed_minute = feed_minute;

  strncpy(prev_mealName, mealName, sizeof(prev_mealName) - 1);
  prev_mealName[sizeof(prev_mealName) - 1] = '\0';

  strncpy(prev_day, day, sizeof(prev_day) - 1);
  prev_day[sizeof(prev_day) - 1] = '\0';

  strncpy(prev_dateISO, dateISO, sizeof(prev_dateISO) - 1);
  prev_dateISO[sizeof(prev_dateISO) - 1] = '\0';
}

// Only store the scheduled portion; the state machine will start feeding in FEED_IDLE.
void startScheduledFeeding(float portionGrams) {
  if (portionGrams <= 0) return;

  scheduledPortionGrams = portionGrams;
  scheduledFeedRequest = true;

  Serial.printf(" Scheduled feeding requested: +%.1f grams\n", portionGrams);
}

// --------------------  FIX: clear stale scheduledFeedRequest --------------------
static bool prevTimeValid = false;

static void clearScheduledFeedRequest(const char* reason) { //motor working on its own problem's fixer
  scheduledFeedRequest = false;
  scheduledPortionGrams = 0.0f;
  Serial.printf(" Cleared scheduledFeedRequest (%s)\n", reason ? reason : "no reason");
}

static void handleTimeRecoveryClear() { //motor working on its own problem's fixer
  bool nowValid = timeIsValid();
  if (!prevTimeValid && nowValid) {
    clearScheduledFeedRequest("time recovered (NO-CLOCK -> CLOCK)");
  }
  prevTimeValid = nowValid;
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(500);

  initDistance();
  initPixels();
  initMotor();

  pinMode(FEED_BUTTON_PIN, INPUT_PULLUP);

  initScale();
  initLocalStorage();

  prefsBootInitAndLoad();

  wipeCreds();

  // provisioning portal in setup (blocking)
  bool networkisconnected = setupWiFiProvisioning();
  if (networkisconnected) {
    wifiEnableAutoReconnect();
    (void)initNTP();
    ntpValid = timeIsValid();
    if (ntpValid) {
      initFirebase();
      firebaseInited = true;
    } else {
      firebaseInited = false;
    }
  } else {
    ntpValid = false;
    firebaseInited = false;
  }

  scheduleInitialOfflineFeed();

  prevTimeValid = timeIsValid();

  Serial.println(" Boot complete");
}

// ---------- Motor + feeding logic (state machine) ----------
void updateMotorAndFeeding() {
  updateMotor();

  bool buttonPressed    = (digitalRead(FEED_BUTTON_PIN) == LOW);
  bool buttonRisingEdge = (buttonPressed && !prevButtonPressed);
  prevButtonPressed = buttonPressed;

  currentWeightGramsRecieved = getWeight();

  if (millis() - lastWeightPrintMs > 3000) {
    Serial.print("weight:");
    Serial.println(currentWeightGramsRecieved);
    lastWeightPrintMs = millis();
  }


  // If we are ACTIVE and we are forced to stop due to motor disabled / container empty,
  // log a stop reason ONCE before we wipe eventId/state.
  if (motorState == MOTOR_DISABLED || containerEmpty) {

    if (feedState == FEED_ACTIVE && !feedingStopNotified) {
      if (currentFeedingEventId < 0) { currentFeedingEventId = makeEventIdEpochSecondsLocal(); }

      if (WiFi.status() == WL_CONNECTED) {
        (void)firebaseLogMealNotification(
            containerEmpty ? "feeding_stopped_empty" : "feeding_stopped_disabled",
            mealName,
            feed_hour,
            feed_minute,
            dueAmount,
            currentFeedingEventId);
      }
      feedingStopNotified = true;
    }

    stopMotor();
    feedState = FEED_IDLE;
    aboveTargetCount = 0;
    motorStartedThisCycle = false;

    timeoutRecoveryPhase = TR_NONE;
    timeoutRecoveryCount = 0;

    currentFeedingEventId = -1;

    // avoid stale state
    curFeedingNoClock = false;
    curFeedingAmountGrams = 0;
    curFeedingStartWeight = 0.0f;

    return;
  }

  switch (feedState) {
    case FEED_IDLE:
      aboveTargetCount = 0;
      motorStartedThisCycle = false;

      if (buttonRisingEdge || scheduledFeedRequest) {
        float portion = buttonRisingEdge ? FEED_PORTION_GRAMS : scheduledPortionGrams;

        // Manual metadata
        if (buttonRisingEdge) {
          time_t now = time(nullptr);
          if (now >= 100000) {
            struct tm tmNow;
            localtime_r(&now, &tmNow);
            feed_hour = tmNow.tm_hour;
            feed_minute = tmNow.tm_min;
          } else {
            feed_hour = 0;
            feed_minute = 0;
          }

          dueAmount = (int)lroundf(portion);
          strncpy(mealName, "manual", sizeof(mealName) - 1);
          mealName[sizeof(mealName) - 1] = '\0';

          setCurrentDayName(day, sizeof(day));
          setCurrentDateISO(dateISO, sizeof(dateISO));
        }

        // consume scheduled request
        scheduledFeedRequest = false;
        scheduledPortionGrams = 0.0f;

        dueAmount = (int)lroundf(portion);

        currentFeedingEventId = makeEventIdEpochSecondsLocal();

        if (WiFi.status() == WL_CONNECTED) {
          (void)firebaseLogMealNotification(
              "feeding_started",
              mealName,
              feed_hour,
              feed_minute,
              dueAmount,
              currentFeedingEventId);
        }


        feedingStopNotified = false;

        // Upload/store previous feeding weights
        if (prev_mealName[0]) {
          if (WiFi.status() == WL_CONNECTED) {
            upload_status = update_weight(prev_dueAmount,
                                          prev_feed_hour,
                                          prev_feed_minute,
                                          prev_mealName,
                                          prev_day,
                                          prev_dateISO,
                                          prev_currentWeightGramsRecieved,
                                          currentWeightGramsRecieved);
          } else {
            if (timeIsValid()) {
              (void)localQueueWeightUpdate(prev_dueAmount,
                                           prev_feed_hour,
                                           prev_feed_minute,
                                           prev_mealName,
                                           prev_day,
                                           prev_dateISO,
                                           prev_currentWeightGramsRecieved,
                                           currentWeightGramsRecieved);
            } else {
              Serial.println(" No-clock mode: skipping file queue (RAM accumulation will handle it).");
            }
            upload_status = true;
          }
        }

        update_prevs();

        reZeroScale();
        updateWeight();
        currentWeightGramsRecieved = getWeight();

        // tag this feeding so we can accumulate when it FINISHES (final weight)
        curFeedingNoClock     = !timeIsValid();
        curFeedingAmountGrams = dueAmount;
        curFeedingStartWeight = currentWeightGramsRecieved;

        timeoutRecoveryPhase = TR_NONE;
        timeoutRecoveryCount = 0;

        feedTargetWeightGrams = currentWeightGramsRecieved + portion;
        feedStartMillis       = millis();
        feedState             = FEED_ACTIVE;

        startMotor();
        motorStartedThisCycle = true;

        Serial.printf(" Feeding started (portion=%.1f, target=%.1f)\n",
                      portion, feedTargetWeightGrams);
      }
      break;

    case FEED_ACTIVE:
      // NOTE: containerEmpty stop is already handled by the guard above,
      // but keeping this block is fine. We'll keep it, but ensure it logs once.
      if (containerEmpty) {
        stopMotor();
        feedState = FEED_IDLE;
        aboveTargetCount = 0;
        motorStartedThisCycle = false;

        timeoutRecoveryPhase = TR_NONE;

        pendingFinalWeight = true;
        motorStoppedAtMs = 0;
        pendingFinalWeightSinceMs = millis(); //  start watchdog timer

        //  log stop due to empty (once)
        if (!feedingStopNotified) {
          if (currentFeedingEventId < 0) { currentFeedingEventId = makeEventIdEpochSecondsLocal(); }
          if (WiFi.status() == WL_CONNECTED) {
            (void)firebaseLogMealNotification(
                "feeding_stopped_empty",
                mealName,
                feed_hour,
                feed_minute,
                dueAmount,
                currentFeedingEventId);
          }
          feedingStopNotified = true;
        }

        currentFeedingEventId = -1;
        break;
      }

      // recovery wiggle handling
      if (timeoutRecoveryPhase != TR_NONE) {
        unsigned long nowMs = millis();

        if (timeoutRecoveryPhase == TR_BACKWARD) {
          if (nowMs - timeoutRecoveryPhaseStartMs >= TIMEOUT_RECOVER_BACK_MS) {
            Serial.print("forward");
            startMotor();
            motorStartedThisCycle = true;

            timeoutRecoveryPhase = TR_FORWARD;
            timeoutRecoveryPhaseStartMs = nowMs;
          }
        } else if (timeoutRecoveryPhase == TR_FORWARD) {
          if (nowMs - timeoutRecoveryPhaseStartMs >= TIMEOUT_RECOVER_FWD_MS) {
            timeoutRecoveryPhase = TR_NONE;
            feedStartMillis = nowMs;
            Serial.println(" Timeout recovery done -> continuing feeding");
          }
        }
        return;
      }

      if (!motorStartedThisCycle) {
        startMotor();
        motorStartedThisCycle = true;
      }

      if (currentWeightGramsRecieved >= feedTargetWeightGrams) {
        aboveTargetCount++;
      } else if (aboveTargetCount > 0) {
        aboveTargetCount--;
      }

      if (aboveTargetCount >= REQUIRED_ABOVE_TARGET) {
        stopMotor();
        feedState = FEED_IDLE;
        motorStartedThisCycle = false;

        timeoutRecoveryPhase = TR_NONE;

        pendingFinalWeight = true;
        motorStoppedAtMs = 0;
        pendingFinalWeightSinceMs = millis(); // ✅ FIX 3: start watchdog timer

        if (currentFeedingEventId < 0) { currentFeedingEventId = makeEventIdEpochSecondsLocal(); }

        if (WiFi.status() == WL_CONNECTED) {
          (void)firebaseLogMealNotification(
              "feeding_success",
              mealName,
              feed_hour,
              feed_minute,
              dueAmount,
              currentFeedingEventId);
        }

        //   already notified for this session
        feedingStopNotified = true;

        currentFeedingEventId = -1;
        Serial.println(" Target reached, motor stopping");
      }
      else if (millis() - feedStartMillis > FEED_TIMEOUT_MS) {

        if (timeoutRecoveryCount >= TIMEOUT_RECOVERY_MAX) {
          stopMotor();
          feedState = FEED_IDLE;
          aboveTargetCount = 0;
          motorStartedThisCycle = false;

          timeoutRecoveryPhase = TR_NONE;

          pendingFinalWeight = true;
          motorStoppedAtMs = 0;
          pendingFinalWeightSinceMs = millis(); //  start watchdog timer

          if (currentFeedingEventId < 0) { currentFeedingEventId = makeEventIdEpochSecondsLocal(); }

          if (WiFi.status() == WL_CONNECTED) {
            (void)firebaseLogMealNotification(
                "feeding_failed_timeout",
                mealName,
                feed_hour,
                feed_minute,
                dueAmount,
                currentFeedingEventId);
          }

          
          feedingStopNotified = true;

          currentFeedingEventId = -1;
          Serial.println("⚠️ Timeout reached (max recoveries) -> motor stopping");
          break;
        }

        timeoutRecoveryCount++;
        weightAtTimeout = currentWeightGramsRecieved;
        aboveTargetCount = 0;

        Serial.printf("⚠️ Timeout reached -> recovery wiggle %d/%d (weight=%.1f, target=%.1f)\n",
                      timeoutRecoveryCount, TIMEOUT_RECOVERY_MAX,
                      weightAtTimeout, feedTargetWeightGrams);

        Serial.print("backward");
        startMotorBackward();
        motorStartedThisCycle = true;

        timeoutRecoveryPhase = TR_BACKWARD;
        timeoutRecoveryPhaseStartMs = millis();
      }

      break;
  }
}

// ---------- main loop ----------
void loop() {
  updateMotor();

  updateDistance();
  updateWeight();

  wifiAutoReconnectTick();

  // ---- Capture final weight only after motor fully stops + settles ----
  if (pendingFinalWeight) {


    if (pendingFinalWeightSinceMs != 0 &&
        (millis() - pendingFinalWeightSinceMs) > FINAL_WEIGHT_MAX_WAIT_MS) {

      Serial.println("⚠️ Final-weight watchdog: motorMoveDone() didn't happen -> forcing final weight capture");

      // Best effort: ensure stop command, then read weight anyway
      stopMotor();
      updateMotor();
      delay(20);

      updateWeight();
      prev_currentWeightGramsRecieved = getWeight();

      pendingFinalWeight = false;
      motorStoppedAtMs = 0;
      pendingFinalWeightSinceMs = 0;

      Serial.print(" Forced final weight captured: ");
      Serial.println(prev_currentWeightGramsRecieved);
    }
    else {
      // Original logic unchanged
      if (motorMoveDone()) {
        if (motorStoppedAtMs == 0) {
          motorStoppedAtMs = millis();
        }
        if (millis() - motorStoppedAtMs >= FINAL_WEIGHT_SETTLE_MS) {
          updateWeight();
          prev_currentWeightGramsRecieved = getWeight();

          if (curFeedingNoClock) {
            if (!noClockAccumHasData) {
              noClockAccumHasData = true;
              noClockAccumTotalGrams = 0;
              noClockAccumSumPrevWeight = 0.0f;
            }

            noClockAccumTotalGrams += curFeedingAmountGrams;
            noClockAccumSumPrevWeight += prev_currentWeightGramsRecieved;

            Serial.printf(" No-clock accum: +%d g (total=%d), +prev=%.1f (sumPrev=%.1f), lastNew=%.1f\n",
                          curFeedingAmountGrams,
                          noClockAccumTotalGrams,
                          curFeedingStartWeight,
                          noClockAccumSumPrevWeight,
                          noClockAccumLastNewWeight);

            curFeedingNoClock = false;
            curFeedingAmountGrams = 0;
            curFeedingStartWeight = 0.0f;
          }

          pendingFinalWeight = false;
          motorStoppedAtMs = 0;
          pendingFinalWeightSinceMs = 0; // ✅ FIX 3: clear watchdog timer

          Serial.print(" Final weight captured: ");
          Serial.println(prev_currentWeightGramsRecieved);
        }
      } else {
        motorStoppedAtMs = 0;
      }
    }
  }

  // ---- Container empty debounce ----
  prevEmpty = containerEmpty;

  bool rawEmpty = isContainerEmpty();

  if (rawEmpty != rawEmptyCandidate) {
    rawEmptyCandidate = rawEmpty;
    rawEmptySinceMs = millis();
  } else {
    if ((millis() - rawEmptySinceMs) >= EMPTY_STABLE_MS) {
      containerEmpty = rawEmptyCandidate;
    }
  }

  // ---- Initial container sync ----
  if (!didInitialContainerSync) {
    pendingContainerStatusUpdate = true;
    pendingContainerEmptyValue = containerEmpty;
    lastContainerStatusPublishAttemptMs = 0;
    didInitialContainerSync = true;
  }

  // ---- Container transitions ----
  if (!prevEmpty && containerEmpty) {

    //   (MINIMAL): if container becomes empty DURING an active feed,
    // log "feeding_stopped_empty" BEFORE we wipe feedState/eventId here.
    if (feedState == FEED_ACTIVE && !feedingStopNotified) {
      if (currentFeedingEventId < 0) { currentFeedingEventId = makeEventIdEpochSecondsLocal(); }
      if (WiFi.status() == WL_CONNECTED) {
        (void)firebaseLogMealNotification(
            "feeding_stopped_empty",
            mealName,
            feed_hour,
            feed_minute,
            dueAmount,
            currentFeedingEventId);
      }
      feedingStopNotified = true;
    }

    motorState = MOTOR_DISABLED;
    feedState = FEED_IDLE;
    stopMotor();
    motorStartedThisCycle = false;

    timeoutRecoveryPhase = TR_NONE;
    timeoutRecoveryCount = 0;

    currentFeedingEventId = -1;

    Serial.println(" Container empty -> motor disabled");

    pendingContainerStatusUpdate = true;
    pendingContainerEmptyValue = true;
    lastContainerStatusPublishAttemptMs = 0;

  } else if (prevEmpty && !containerEmpty) {
    motorState = MOTOR_ENABLED;
    Serial.println(" Container refilled -> motor enabled");

    pendingContainerStatusUpdate = true;
    pendingContainerEmptyValue = false;
    lastContainerStatusPublishAttemptMs = 0;
  }

  // ---- Publish container status (retry) ----
  if (feedState == FEED_IDLE && pendingContainerStatusUpdate &&
      (lastContainerStatusPublishAttemptMs == 0 ||
       (millis() - lastContainerStatusPublishAttemptMs) >= CONTAINER_STATUS_PUBLISH_RETRY_MS)) {

    lastContainerStatusPublishAttemptMs = millis();

    if (WiFi.status() == WL_CONNECTED) {
      bool ok = firebasePublishContainerEmpty(pendingContainerEmptyValue);
      if (ok) {
        pendingContainerStatusUpdate = false;
      }
    }
  }

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  updateNeoPixel(containerEmpty, wifiConnected);

  // -------------------- AUTO portal open (DEFERRED) --------------------
  if (WiFi.status() != WL_CONNECTED) {
    if (offlineSinceMs == 0) offlineSinceMs = millis();
    firebaseInited = false;
  } else {
    offlineSinceMs = 0;
    portalPending = false;      // cancel if WiFi returned
    motorDoneSinceMs = 0;
  }

  // Arm portalPending after being offline long enough (do NOT block yet)
  if (WiFi.status() != WL_CONNECTED &&
      offlineSinceMs != 0 &&
      (millis() - offlineSinceMs) >= OPEN_PORTAL_AFTER_MS &&
      feedState == FEED_IDLE &&
      !scheduledFeedRequest &&
      !pendingFinalWeight) {

    if (!portalPending) {
      portalPending = true;
      motorDoneSinceMs = 0;
      Serial.println(" No WiFi for long time -> portalPending armed (will open ONLY after motor fully stops)");
    }
  }

  // If portal pending: only open when motor is fully done + stable
  if (portalPending) {
    if (motorMoveDone() && !pendingFinalWeight) {
      if (motorDoneSinceMs == 0) motorDoneSinceMs = millis();
    } else {
      motorDoneSinceMs = 0;
    }

    const bool motorStableStopped =
        (motorDoneSinceMs != 0 && (millis() - motorDoneSinceMs) >= MOTOR_DONE_STABLE_MS);

    if (WiFi.status() != WL_CONNECTED &&
        feedState == FEED_IDLE &&
        !scheduledFeedRequest &&
        !pendingFinalWeight &&
        motorStableStopped) {

      Serial.println(" portalPending: motor stopped + stable -> opening provisioning portal (blocking)...");
      portalPending = false;
      motorDoneSinceMs = 0;
      offlineSinceMs = 0;

      // Important: do NOT stop motor here. It's already stopped.
      // Also clear any stale scheduled request before blocking.
      clearScheduledFeedRequest("before portal (deferred)");

      bool ok = setupWiFiProvisioning(); // BLOCKING portal

      if (ok) {
        (void)initNTP();
        ntpValid = timeIsValid();
        if (ntpValid) {
          prefsClearOfflineFeedMarker();
          initFirebase();
          firebaseInited = true;
        } else {
          firebaseInited = false;
        }
      } else {
        ntpValid = timeIsValid();
        firebaseInited = false;
      }
    }
  }

  // If idle, keep trying NTP non-blocking
  if (feedState == FEED_IDLE) {
    ntpValid = ntpTick();
    if (ntpValid) {
      prefsClearOfflineFeedMarker();
      if (!firebaseInited && WiFi.status() == WL_CONNECTED) {
        initFirebase();
        firebaseInited = true;
      }
    }
  }

  //  clear stale scheduledFeedRequest on NO-CLOCK -> CLOCK transition
  handleTimeRecoveryClear();

  updateMotor();

  // ---- Scheduling decisions ----
  if (feedState == FEED_IDLE) {

    //  If WiFi+clock are back and we have RAM no-clock data -> upload ONE row
    if (WiFi.status() == WL_CONNECTED && timeIsValid() && noClockAccumHasData) {

      time_t now = time(nullptr);
      struct tm tmNow;
      localtime_r(&now, &tmNow);

      char nowDay[10];
      char nowDate[11];
      getCurrentDayNameNow(nowDay, sizeof(nowDay));
      getCurrentDateISONow(nowDate, sizeof(nowDate));

      int nowHour = tmNow.tm_hour;
      int nowMin  = tmNow.tm_min;

      char meal[32];
      snprintf(meal, sizeof(meal), "offline_%s_%02d-%02d", nowDate, nowHour, nowMin);
      noClockAccumLastNewWeight = getWeight();

      bool ok = update_weight(
          noClockAccumTotalGrams,
          nowHour,
          nowMin,
          meal,
          nowDay,
          nowDate,
          noClockAccumSumPrevWeight,
          noClockAccumLastNewWeight
      );

      if (ok) {
        Serial.println(" Uploaded NO-CLOCK accumulated entry -> clearing RAM accumulator");
        noClockAccumHasData = false;
        noClockAccumTotalGrams = 0;
        noClockAccumSumPrevWeight = 0.0f;
        noClockAccumLastNewWeight = 0.0f;
      } else {
        Serial.println(" Upload of NO-CLOCK accumulated entry failed -> will retry later");
      }
    }

    // 1) If online + idle -> try flushing normal offline queue (throttled)
    if (WiFi.status() == WL_CONNECTED && localWeightsQueueExists()) {
      if (lastQueueSyncMs == 0 || (millis() - lastQueueSyncMs) >= QUEUE_SYNC_INTERVAL_MS) {
        lastQueueSyncMs = millis();
        (void)localFlushWeightsQueue(update_weight);
      }
    }

    // 2) If we have real time -> normal schedule (Firebase / Local schedule)
    if (ntpValid) {

      if (firebaseIsDatabaseConnected()) {
        firebaseLoop();

        bool due = firebaseGetDueFeeding(dueAmount, feed_hour, feed_minute,
                                         mealName, sizeof(mealName));
        if (due) {
          Serial.printf(" Due meal: %s at %02d:%02d (%d g)\n",
                        mealName, feed_hour, feed_minute, dueAmount);

          if (isDuplicateScheduledMinute(mealName, feed_hour, feed_minute)) {
            Serial.println(" Suppressed duplicate scheduled feeding (same minute, reconnect)");
          } else {
            setCurrentDayName(day, sizeof(day));
            setCurrentDateISO(dateISO, sizeof(dateISO));
            startScheduledFeeding((float)dueAmount);
          }
        }
      } else {
        //Serial.println("couldnt reach firebase (offline db) -> using LOCAL schedule");

        bool dueLocal = localGetDueFeeding(dueAmount, feed_hour, feed_minute,
                                           mealName, sizeof(mealName));
        if (dueLocal) {
          Serial.printf(" (LOCAL) Due meal: %s at %02d:%02d (%d g)\n",
                        mealName, feed_hour, feed_minute, dueAmount);

          if (isDuplicateScheduledMinute(mealName, feed_hour, feed_minute)) {
            Serial.println(" Suppressed duplicate scheduled feeding (same minute, reconnect)");
          } else {
            setCurrentDayName(day, sizeof(day));
            setCurrentDateISO(dateISO, sizeof(dateISO));
            startScheduledFeeding((float)dueAmount);
          }
        }
      }
    }

    // 3) No real time -> Offline interval feeding (+ reboot safety)
    else {
      if (!scheduledFeedRequest &&
          motorState == MOTOR_ENABLED &&
          !containerEmpty) {

        const uint32_t nowMs = millis();
        if ((int32_t)(nowMs - nextOfflineFeedMs) >= 0) {

          prefsSetLastOfflineFeedBoot(bootCounter);

          strncpy(mealName, "offline_6h", sizeof(mealName) - 1);
          mealName[sizeof(mealName) - 1] = '\0';

          feed_hour = 0;
          feed_minute = 0;
          dueAmount = (int)lroundf(FEED_PORTION_GRAMS);

          Serial.println(" (OFFLINE) Interval feeding triggered (no real time)");

          startScheduledFeeding(FEED_PORTION_GRAMS);
          nextOfflineFeedMs = nowMs + OFFLINE_INTERVAL_MS;
        }
      }
    }
  }

  updateMotorAndFeeding();
  delay(1);
}
