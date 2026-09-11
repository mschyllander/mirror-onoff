# Strömmätning

![Testkörning med filtrerad ström och ON/OFF-trösklar](images/current-test.png)

## Underlag

Den bifogade grafen heter **Uppmätt spegelström under testkörning**. X-axeln visar sekunder från start och Y-axeln ström i mA. Kurvan är märkt *Filtrerad ström*. Någon rådatafil har inte bifogats. Firmware 1.3 finns nu i repot och använder ett glidande medelvärde över tio strömmätningar med 100 ms avläsningsintervall. Det är inte fastställt att just denna version skapade grafen.

## Avläsning

| Observation | Ungefärligt värde |
| :--- | :--- |
| ON-tröskel enligt teckenförklaringen | 31 mA |
| OFF-tröskel enligt teckenförklaringen | 26 mA |
| Längre platå, ungefär 135–248 s | 315 mA |
| Högre platåer | 500–520 mA |
| Högsta synliga topp | 830 mA |

Platåer, tider och toppvärde är visuella uppskattningar från bilden. Tröskelvärdena är uttryckligen angivna i grafens teckenförklaring.

## Logik i firmware 1.3

Skillnaden mellan trösklarna är 5 mA. Koden bekräftar ON när filtrerad ström ligger på minst 31 mA i 700 ms, och OFF vid högst 26 mA i 1000 ms. Mellan trösklarna behålls ett känt tillstånd. Timern startar vid bekräftat ON när automatiken är aktiv och återstartsspärren tillåter det.

Kodens inledande kommentar anger cirka 20–21,5 mA för släckt spegel och 36–40 mA för lägsta tända nivå. Detta är projektets antecknade mätvärden, inte nya mätningar i detta repo.

För att återskapa testgrafen behövs rådata och anteckningar om händelserna under körningen samt bekräftelse på vilken firmwareversion som användes.

[Firmware och fullständiga timerregler](../firmware/README.md) · [Tillbaka till projektet](../README.md)
