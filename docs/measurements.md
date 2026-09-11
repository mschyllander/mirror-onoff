# Strömmätning

![Testkörning med filtrerad ström och ON/OFF-trösklar](images/current-test.png)

## Underlag

Den bifogade grafen heter **Uppmätt spegelström under testkörning**. X-axeln visar sekunder från start och Y-axeln ström i mA. Kurvan är märkt *Filtrerad ström*. Någon rådatafil eller kod för filtreringen har inte bifogats.

## Avläsning

| Observation | Ungefärligt värde |
| :--- | :--- |
| ON-tröskel enligt teckenförklaringen | 31 mA |
| OFF-tröskel enligt teckenförklaringen | 26 mA |
| Längre platå, ungefär 135–248 s | 315 mA |
| Högre platåer | 500–520 mA |
| Högsta synliga topp | 830 mA |

Platåer, tider och toppvärde är visuella uppskattningar från bilden. Tröskelvärdena är uttryckligen angivna i grafens teckenförklaring.

## Tolkning som behöver bekräftas

Skillnaden mellan ON- och OFF-tröskeln är 5 mA. En möjlig användning är att växla till ON över den övre tröskeln, till OFF under den undre och behålla tillståndet däremellan. Detta är en möjlig tolkning av grafen, inte en beskrivning av verifierad firmware.

För att återskapa testet behövs rådata, samplingsintervall, filterdefinition, sensorkalibrering och anteckningar om vad som hände under körningen.

[Tillbaka till projektet](../README.md)
