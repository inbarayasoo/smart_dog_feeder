#ifndef FIREBASEMANAGER_H
#define FIREBASEMANAGER_H

#include <stddef.h>
#include <stdint.h>  

void initFirebase();
void firebaseLoop();

// A single feeding schedule entry (max 6 per day)
struct FeedingScheduleEntry {
  bool enabled;
  int hour;
  int minute;
  int amountGrams;
  char mealName[30];
};

// Returns true if a feeding is due right now; outputs the amount in grams
// This will return true only once per entry per day (it auto "locks" after firing).
bool firebaseGetDueFeeding(int &amountOut,
                           int &feed_hour,
                           int &feed_minute,
                           char *mealNameOut,
                           size_t mealNameOutSize);

void firebaseSetContainerEmpty(bool empty);


bool update_weight(int amount_grams,
                   int feed_hour,
                   int feed_minute,
                   const char *meal_name,
                   const char *day,
                   const char *date,      // "YYYY-MM-DD"
                   float prev_current_weight,
                   float new_current_weight);

bool firebasePublishContainerEmpty(bool emptyNow);



bool firebaseIsDatabaseConnected();

bool firebaseLogMealNotification(const char* type,
                                 const char* mealName,
                                 int hour,
                                 int minute,
                                 int amountGrams,
                                 int32_t eventId);

#endif
