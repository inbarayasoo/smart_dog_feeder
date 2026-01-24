#pragma once
#include <Arduino.h>

// ---------- LittleFS init ----------
bool initLocalStorage();

// ---------- Schedule cache ----------
bool localStoreScheduleIfChanged(const char* json);
bool localLoadSchedule(String &outJson);

// ---------- Offline schedule execution ----------
bool localGetDueFeeding(int &amountOut,
                        int &feed_hour,
                        int &feed_minute,
                        char *mealNameOut,
                        size_t mealNameOutSize);

// ---------- Offline stats queue (weights) ----------
bool localQueueWeightUpdate(int dueAmount,
                            int feed_hour,
                            int feed_minute,
                            const char* mealName,
                            const char* day,
                            const char* dateISO,
                            float prevWeight,
                            float currentWeight);

// Upload-callback signature (we will pass update_weight here)
typedef bool (*WeightUploadFn)(int amount_grams,
                              int feed_hour,
                              int feed_minute,
                              const char *meal_name,
                              const char *day,
                              const char *date,
                              float prev_current_weight,
                              float new_current_weight);

// Flush local queue to Firebase:
// - uploads line-by-line
// - if a line fails: keeps that line + remaining lines (no duplicates)
// - if all succeed: deletes the queue file
bool localFlushWeightsQueue(WeightUploadFn uploadFn);

// Optional: quick check
bool localWeightsQueueExists();
