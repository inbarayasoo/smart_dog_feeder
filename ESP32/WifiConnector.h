#ifndef WIFICONNECTOR_H
#define WIFICONNECTOR_H

bool setupWiFiProvisioning();     // boot/demo: ALWAYS portal
void wifiEnableAutoReconnect();   // runtime: phone-like reconnect
void wifiAutoReconnectTick();     // call in loop when offline
bool isConnected();               // real status (WiFi.status)
void wipeCreds();
#endif