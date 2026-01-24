#ifndef NTPMANAGER_H
#define NTPMANAGER_H



// Function to initialize NTP synchronization
bool initNTP();

// Function to print the current local time for testing
void printCurrentTime();

// Utility function to get the current time structure
bool getLocalTimeInfo(struct tm *info);
// ✅ NEW: non-blocking NTP service (call from loop)
bool ntpTick();

#endif
