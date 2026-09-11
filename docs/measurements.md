# Current measurements

![Test run showing filtered current and ON/OFF thresholds](images/current-test.png)

## Source material

The original plot is in Swedish. Its title translates to **Measured mirror current during a test run**. The horizontal axis shows seconds since startup; the vertical axis shows current in mA. The curve is labeled *Filtered current*.

| Original label | English |
| :--- | :--- |
| Tid från start (s) | Time since startup (s) |
| Ström (mA) | Current (mA) |
| Filtrerad ström | Filtered current |
| ON-tröskel 31 mA | ON threshold 31 mA |
| OFF-tröskel 26 mA | OFF threshold 26 mA |

No raw data file was supplied. Firmware 1.3 uses a moving average of ten current samples with a 100 ms read interval. It has not been established whether this exact version produced the plot.

## Reading the plot

| Observation | Approximate value |
| :--- | :--- |
| ON threshold stated in the legend | 31 mA |
| OFF threshold stated in the legend | 26 mA |
| Longer plateau, roughly 135–248 s | 315 mA |
| Higher plateaus | 500–520 mA |
| Highest visible peak | 830 mA |

Plateaus, times and the peak value are visual estimates from the image. Threshold values are explicitly stated in the legend.

## Firmware 1.3 logic

The thresholds are 5 mA apart. The code confirms ON when filtered current stays at or above 31 mA for 700 ms, and OFF at or below 26 mA for 1000 ms. Between thresholds, a known state is retained. Confirmed ON starts the timer when automatic mode is enabled and the rearming condition allows it.

The introductory code comment records approximately 20–21.5 mA with the mirror light off and 36–40 mA at its lowest on level. These are the project's recorded measurements, not new measurements made for this repository.

Reproducing the plot requires raw data, notes about events during the run and confirmation of the firmware version used.

[Firmware and complete timer behavior](../firmware/README.md) · [Back to the project](../README.md)
