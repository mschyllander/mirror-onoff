# Firmware · MrMatzo Mirror Controller 1.3

[Open the Arduino sketch](esp_12V_mirror_timer/esp_12V_mirror_timer.ino)

The original source is included without changes. The sketch is stored in a matching folder, ready to open in Arduino IDE. This documentation describes the implementation; the program has not been compiled or run as part of this documentation work. The original web interface and status messages are in Swedish.

## Build requirements

The code targets ESP8266 and includes the following libraries:

| Library / header | Purpose |
| :--- | :--- |
| ESP8266WiFi, ESP8266WebServer, ESP8266mDNS | Wi-Fi, HTTP and local hostname |
| Wire | I²C |
| LittleFS | Settings, session number and logs |
| WiFiManager | Network configuration portal |
| Adafruit_INA219 | Current, voltage and power measurements |

The exact board profile, flash layout, ESP8266 core version and library versions from the working build still need to be recorded for reproducibility. The web interface's HTML, CSS and JavaScript are embedded in the sketch. Serial uses 921600 baud.

## Operation

1. At startup, the program turns on the mirror supply and attempts to connect to Wi-Fi through WiFiManager.
2. When network setup is needed, the configuration portal is named **Mirror-Setup**. Its timeout is 300 seconds; a failed connection causes a restart.
3. Once connected, open [mirror.local](http://mirror.local), or use the device IP address printed to Serial.
4. Set a duration of 1–1440 minutes. The default is 10 minutes. Changing the duration during an active timer restarts the countdown with the new duration.
5. Turn on the mirror light. Confirmed current consumption starts the timer when automatic mode is enabled and rearming is allowed.

The web app provides automatic mode on/off, manual supply on/off, a three-second power cycle, log viewing, CSV download, log clearing and Wi-Fi settings reset. Status is fetched every second.

## Detection and power cycling

| Parameter | Value in the code |
| :--- | :--- |
| Sensor interval | 100 ms, subject to main-loop scheduling |
| Current filter | Moving average of 10 samples; negative raw readings are clamped to 0 |
| Light on | Filtered current ≥ 31 mA for 700 ms |
| Light off | Filtered current ≤ 26 mA for 1000 ms |
| Between thresholds | A known state is retained; an unknown state remains unknown |
| Automatic power interruption | 3 seconds |
| State detection lockout | 5 seconds after startup or power restoration |
| Sensor calibration | `setCalibration_32V_2A()` |

After an automatic timeout, the light must be confirmed off before another timer can start. Manual power cycling and manual power-on clear `requireOffBeforeRearm`. Sensor readings continue during the detection lockout.

The timer duration persists across restarts. Automatic mode and an active countdown do not; the program starts with automatic mode and supply power enabled. If the sensor is not found at startup, a sensor error is displayed and automatic current detection is unavailable. Startup waits for Wi-Fi before normal timer handling begins.

## Logging

LittleFS stores `/settings.txt`, `/session.txt`, `/mirror_log.csv` and the rotated `/mirror_log_old.csv`. Measurements are logged every ten seconds and when events occur. When the size check finds the current log at or above 200 KiB, it is rotated, replacing any previous rotated log.

CSV fields: `session,uptime_ms,raw_mA,filtered_mA,bus_V,load_V,power_mW,state,event`.

Time is uptime in milliseconds, not a calendar timestamp. The code calculates `load_V` as bus voltage plus shunt voltage; the field name is reproduced from the program. The web download contains the current log.

## Web endpoints

| Path | Function |
| :--- | :--- |
| `/` | Web app |
| `/status` | JSON status |
| `/settime?minutes=10` | Set timer duration |
| `/auto/toggle` | Toggle automatic mode |
| `/power/on`, `/power/off` | Manual supply control |
| `/reset` | Interrupt power for 3 seconds |
| `/log`, `/log/download`, `/log/clear` | View, download and clear logs |
| `/wifi-reset` | Clear Wi-Fi settings and restart |

This version serves HTTP on port 80 without authentication. Anyone who can reach the server can use its controls.

[Back to the project](../README.md)
