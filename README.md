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

## Connection diagram:

## Project Poster:
 
This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion
https://icst.cs.technion.ac.il/
