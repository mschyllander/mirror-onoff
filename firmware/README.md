# Firmware · MrMatzo Mirror Controller 1.4.1

[Arduino sketch](esp_12V_mirror_timer/esp_12V_mirror_timer.ino) · [Release notes](../CHANGELOG.md)

## Install once by USB, then update over Wi-Fi

Version 1.3 has no OTA receiver. It must be replaced over USB once. Version 1.4.0 adds a browser upload page at `/update`; future updates can use that page.

1. Copy `DeviceConfig.example.h` to `DeviceConfig.h` in the sketch folder.
2. Set `MIRROR_ADMIN_PASSWORD` for the admin/update pages and `MIRROR_AP_PASSWORD` for direct Wi-Fi (8–63 ASCII characters). These are independent as of 1.4.1. The private header is ignored by Git; keep it for future builds.
3. Open `esp_12V_mirror_timer.ino` in Arduino IDE. Select **NodeMCU 1.0 (ESP-12E Module)** for the photographed board, and verify the actual board and flash capacity before uploading.
4. Compile and upload over USB using the settings below. Preserve the filesystem and Wi-Fi settings; do not erase all flash.
5. Open [mirror.local](http://mirror.local) on the same network, or use the device IP address. For a direct connection, join **Mirror-Setup** using your configured Wi-Fi password and open [192.168.4.1](http://192.168.4.1).
6. Select **UPPDATERA**, sign in as `admin` with your configured admin password, select the firmware `.bin` and install it. Keep the controller powered until it restarts, then check the firmware version at `/status`.

The spelling is `mirror.local`. Hostname resolution depends on the client/network; the IP address is the fallback. Direct Wi-Fi does not require a router or internet access. A phone may report that Mirror-Setup has no internet; stay connected to use the controller.

### Build verified for 1.4.0

| Component | Version / setting |
| :--- | :--- |
| ESP8266 Arduino core | 3.1.2 |
| Board FQBN | `esp8266:esp8266:nodemcuv2` |
| Flash layout | Board default: 4 MB flash, 2 MB filesystem |
| Adafruit INA219 | 1.2.3 |
| Adafruit BusIO | 1.17.4 |
| Serial monitor | 921600 baud |

The remaining libraries are supplied by the ESP8266 core: WiFi, WebServer, mDNS, Wire, LittleFS and Updater. WiFiManager is no longer required. The web app is embedded in the sketch.

```sh
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 firmware/esp_12V_mirror_timer
```

Use the same flash layout as the installed firmware to retain logs/settings. The project device was read back on 2026-09-14: 4 MB flash with LittleFS from `0x200000` to `0x3FA000`, matching this profile. A binary built with your private header contains the password and should not be published as a generic release artifact.

## Automatic shutdown recovery

The previous firmware could cut power successfully, restore it to an illuminated mirror and then wait forever for OFF with no active timer. Version 1.4.0 retains the OFF confirmation requirement but adds a bounded recovery sequence:

| Attempt | Power off | After restoration |
| :--- | :--- | :--- |
| 1 | 3 seconds | Wait 5 seconds, then confirm light state |
| 2, if needed | 10 seconds | Wait 5 seconds, then confirm light state |
| 3, if needed | 30 seconds | Wait 5 seconds, then confirm light state |

A confirmed ON starts the next attempt immediately. If the state remains unknown for 15 seconds after restoration, the next attempt starts as well. After the third failure, 12 V remains off with **AVSTÄNGD EFTER SLÄCKFEL**. Manual power-on acknowledges the fault. If LittleFS is working, `/shutdown_fault.txt` preserves the fault across a controller restart; a filesystem failure prevents that persistence.

Confirmation of OFF clears the recovery sequence and allows the next normal timer. Manual OFF cancels any pending power restoration. Manual power cycling remains a single three-second interruption and clears the automatic rearm condition.

## Detection and timer settings

| Parameter | Value |
| :--- | :--- |
| Timer duration | 1–1440 minutes; default 10 minutes |
| Sensor interval | 100 ms, subject to main-loop scheduling |
| Filter | Moving average of 10 samples; negative raw readings clamped to 0 |
| Light on | Filtered current ≥ 31 mA for 700 ms |
| Light off | Filtered current ≤ 26 mA for 1000 ms |
| Between thresholds | Known state retained; unknown state remains unknown |
| Sensor calibration | `setCalibration_32V_2A()` |

Turning off the light stops an active timer. Changing the duration during a countdown starts a fresh countdown. Duration persists across restart; automatic mode and countdown progress do not. Automatic mode starts enabled. A latched shutdown fault keeps the supply off at boot; otherwise supply power is enabled.

If the sensor is missing at startup, the UI reports a sensor error and normal current detection is unavailable. This firmware does not provide an independent hardware cutoff or complete sensor-failure protection.

## Wi-Fi and update behavior

- The password-protected **Mirror-Setup** access point remains available alongside the existing home Wi-Fi connection.
- `/wifi` lets an authenticated administrator save new router credentials. Existing credentials from 1.3 are reused. Timer processing no longer waits for router connection or a blocking setup portal.
- OTA and Wi-Fi configuration use HTTP Digest authentication and a per-boot form token. Firmware uploads accept `.bin` files and write only the firmware area, not a filesystem image.
- An authorized firmware upload turns the mirror supply off and cancels pending automatic restoration. Failed or aborted uploads leave it off. A successful upload restarts the controller.
- Keep using the same private password header when building updates, unless intentionally changing a password. No device password is committed to this repository or returned by `/status`.
- Ordinary mirror controls remain unauthenticated on the local network, as in 1.3. HTTP is not encrypted; do not expose the controller through internet port forwarding. OTA images are not cryptographically signed.

## Logs and endpoints

Measurements are logged every ten seconds and at events. LittleFS retains `/mirror_log.csv` and one rotated `/mirror_log_old.csv`; rotation occurs when the current file reaches 200 KiB. The previous file is now available for download too. Rotation is finite retention, not a permanent archive.

CSV fields remain `session,uptime_ms,raw_mA,filtered_mA,bus_V,load_V,power_mW,state,event`. Time is uptime in milliseconds. `load_V` follows the original code's calculation of bus voltage plus shunt voltage.

| Path | Function |
| :--- | :--- |
| `/`, `/status` | Web app and JSON status |
| `/settime?minutes=10`, `/auto/toggle` | Duration and automatic mode |
| `/power/on`, `/power/off`, `/reset` | Manual supply control and power cycle |
| `/log`, `/log/download` | Current log |
| `/log/old/download` | Previous rotated log |
| `/log/clear` | Clear both log files |
| `/update` | Authenticated firmware upload |
| `/wifi` | Authenticated Wi-Fi configuration |
| `/wifi-reset` | Redirect to configuration; no longer erases credentials |

JSON status adds `shutdownFault`, `shutdownAttempt`, `otaInProgress` and `apIP`. New log events include `SHUTDOWN_RETRY_2`, `SHUTDOWN_RETRY_3`, `SHUTDOWN_CONFIRMED_OFF`, `SHUTDOWN_FAILED_LATCHED_OFF`, `OTA_STARTED`, `OTA_SUCCESS`, `OTA_FAILED`, `OTA_ABORTED` and `OTA_TIMEOUT`.

## Validation

```sh
python tests/test_controller.py
python tests/test_ota.py
```

These tests compile the actual sketch functions with C++ hardware stubs. They cover the observed 524 mA recovery failure, normal rearming, bounded retries, manual cancellation, unknown state, a missing sensor during recovery, threshold timing, millis rollover and filtering. Upload tests cover authentication, form tokens, missing/wrong files, flash errors, aborts and successful retry.

The firmware compiles for the profile above. On 2026-09-14, version 1.4.0 was installed over USB on the project device after a full flash backup. An authenticated OTA transfer of the same firmware completed successfully, followed by a verified reboot and `OTA_SUCCESS` in the log. The timer setting and filesystem were retained. Both the local hostname and LAN IP responded, the INA219 was detected, and unauthenticated access to `/update` returned HTTP 401. A separate direct Wi-Fi connection and physical mirror shutdown/retry test remain outstanding.

[Back to the project](../README.md)
