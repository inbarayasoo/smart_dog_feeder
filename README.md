## Pet Feeder Project by : Inbar Ayaso, Ophir Mizrahi, Shira Hassan 
  
## Details about the project:
An IoT smart pet feeder that automates scheduled and manual feeding with real-time monitoring, designed for reliability even under unstable Wi-Fi and power conditions. The system combines an ESP32 controller (stepper motor + sensors) with a Flutter mobile app and a Firebase backend to manage feeding schedules, log events, and provide live status updates.

**Key Features**
* User-Defined Feeding Schedule: Set feeding times and portion sizes from the app so the pet can be fed while the owner is away.
* Automated Dispensing Mechanism: A motorized hatch/dispensing system releases food into the bowl automatically.
* Food Weight Monitoring: Measures how much food is in the dish using a load cell, allowing verification that the correct amount was dispensed.
* Usage Analytics & Statistics: Tracks feeding events (time + amount) to help confirm consistent and proper feeding behavior.
* Offline Mode: If Wi-Fi fails, the feeder continues operating automatically according to the last saved schedule.
* Notifications & Alerts: Sends alerts for maintenance needs (e.g., refill required).
* Food Level Detection: Monitors the distance to the food surface to detect low/empty container states without manual checking.
* LED Status Indicators:
    Red LED when the food container is empty.
    Blue LED when there is no Wi-Fi connection, providing immediate offline indication without opening the app.
 
## Folder description :
* ESP32: source code for the esp side (firmware).
* Documentation: wiring diagram + basic operating instructions
* Unit Tests: tests for individual hardware components (input / output devices)
* flutter_app : dart code for our Flutter app.
* Parameters: contains description of parameters and settings that can be modified IN YOUR CODE
* Assets: link to 3D printed parts, Audio files used in this project, Fritzing file for connection diagram (FZZ format) etc

## ESP32 SDK version used in this project: 

## Arduino/ESP32 libraries used in this project:
* XXXX - version XXXXX
* XXXX - version XXXXX
* XXXX - version XXXXX

* Adafruit GFX Library by Adafruit - version 1.12.4

Library Name,Author,Version
AccelStepper,Mike McCauley,1.64
Adafruit BusIO,Adafruit,1.17.4
Adafruit DMA neopixel library,Adafruit,1.3.3
Adafruit GFX Library,Adafruit,1.12.4
Adafruit NeoPixel,Adafruit,1.15.2
Adafruit SH110X,Adafruit,2.1.14
Adafruit SSD1306,Adafruit,2.5.15
Adafruit_VL53L0X,Adafruit,1.2.4
ArduinoJson,Benoit Blanchon,7.4.2
ESP_SSLClient,Mobizt,3.1.2
Firebase ESP32 Client,Mobizt,4.4.17
FirebaseClient,Mobizt,2.2.5
HX711,Rob Tillaart,0.6.3
WiFiManager,tzapu,2.0.17

## Connection diagram:
![Connection diagram](./Assets/diagram.jpeg)
## Project Poster:

![Poster of the project](./Assets/poster.png)
 
This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion
https://icst.cs.technion.ac.il/
