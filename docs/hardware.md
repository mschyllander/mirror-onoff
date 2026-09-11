# Hardware

This overview is based on the project photos and firmware version 1.3. It is a component inventory, not a verified wiring diagram.

| Part | Identification from the photos | Still to document |
| :--- | :--- | :--- |
| Controller | ESP8266, module marked ESP-12E; board with D0–D8 and micro-USB | Exact board variant and power connection |
| Current sensor | INA219 DC Current Sensor, shunt marked R100 | Physical I²C address and wiring; code uses the default constructor and 32 V / 2 A calibration |
| Transistor | Separate component marked IRLB8721 | Confirm its installation and connections in the MOSFET switching circuit |
| DC/DC module | Adjustable module with a trimmer potentiometer | Exact model and configured input/output voltages |
| White power adapter | Royal BI24G-120200-AdV, output 12.0 V DC / 2.0 A / 24.0 W | Which adapter is used in the final build |
| Black power adapter | Linksys adapter visible beside the enclosure | Ratings and role in the project |
| Enclosure | Printed case with a separate screw-fastened lid | Material, screw sizes and mounting dimensions |

## GPIO assignments in the source

| Signal | Board pin | GPIO | Connection described in the source |
| :--- | :--- | :--- | :--- |
| MOSFET control | D1 | 5 | Gate through 100 Ω; gate to GND through 10 kΩ, according to the code comment |
| INA219 SDA | D2 | 4 | I²C data |
| INA219 SCL | D5 | 14 | I²C clock |

`setMirrorPower()` drives D1 HIGH for power on and LOW for power off. These pins are used in the implementation; the resistor values come from the introductory code comment. The physical wiring still needs to be checked against the build.

## Wiring

The photos do not establish the complete wiring. A reproducible build needs:

- The supply path through the mirror, sensor and controller.
- INA219 connections: VCC, GND, SDA, SCL, VIN+ and VIN−.
- IRLB8721 connections and any supporting components.
- The DC/DC module's configured output voltage and connection to the controller.
- Connector polarity and confirmation of the GPIO assignments above.

Do not infer connections solely from wire colors in the photos.

[Component photos](gallery.md) · [Back to the project](../README.md)
