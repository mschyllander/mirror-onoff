<div align="center">

# MIRROR / ON·OFF

### Ett elektronikprojekt för spegeln.
Strömmätning · ESP8266 · Egen 3D-printad kapsling

[Hårdvara](docs/hardware.md) · [Mätning](docs/measurements.md) · [Kapsling](hardware/enclosure/) · [Galleri](docs/gallery.md)

![Monterad kapsling för spegelprojektet](docs/images/enclosure-assembled.jpg)

**12 V matning på fotograferad adapter** &nbsp; / &nbsp; **INA219** &nbsp; / &nbsp; **ESP-12E**

</div>

## Projektet

Mirror On/Off samlar arbetet med elektronik till en spegel: prototypen, komponenterna, en strömmätning under test och modellen till en egen kapsling. Här finns underlaget från arbetsbänken samlat för fortsatt utveckling och dokumentation.

Bilderna visar ett ESP8266-kort, en INA219-strömsensor, en justerbar DC/DC-modul och en separat IRLB8721-komponent. Testgrafen visar spegelns ström över ungefär fem minuter, med markerade trösklar för ON och OFF.

> **Projektstatus:** Hårdvara, bilder och 3D-modell finns här. Firmware och verifierat kopplingsschema återstår att lägga till. Den exakta styrfunktionen behöver beskrivas av projektägaren.

## Från prototyp till kapsling

<table>
<tr>
<td width="50%"><img src="docs/images/prototype.jpg" alt="Elektronikprototyp på arbetsbänken"></td>
<td width="50%"><img src="docs/images/enclosure-open.jpg" alt="3D-printad kapsling med separat lock"></td>
</tr>
<tr><td><b>Elektroniken</b><br>Styrkort, mätmodul och spänningsomvandlare.</td><td><b>Kapslingen</b><br>Printad låda med lock, skruvfästen och kabelurtag.</td></tr>
</table>

## Mätningen

![Uppmätt spegelström under testkörning](docs/images/current-test.png)

| Parameter | Visat i underlaget |
| :--- | :--- |
| ON-tröskel | 31 mA |
| OFF-tröskel | 26 mA |
| Skillnad mellan trösklar | 5 mA |
| Testlängd | Cirka 295 sekunder |

Grafen visar både stabila strömnivåer och korta toppar. Två olika trösklar kan användas för hysteres, men firmware behövs för att fastställa den faktiska logiken. [Läs mätanteckningarna →](docs/measurements.md)

## Utforska projektet

| Innehåll | Här finns det |
| :--- | :--- |
| Komponenter och kvarvarande kopplingsuppgifter | [Hårdvara](docs/hardware.md) |
| Testgraf och tolkning | [Mätningar](docs/measurements.md) |
| Alla projektbilder | [Bildgalleri](docs/gallery.md) |
| Modell för kapslingen | [Box_mirror_onoff.3mf](hardware/enclosure/Box_mirror_onoff.3mf) |
| Plats för befintlig programvara | [Firmware](firmware/README.md) |
| Återstående dokumentation | [Nästa steg](docs/next-steps.md) |

## Använd underlaget

Öppna 3MF-filen i ett kompatibelt CAD- eller slicerprogram för att granska kapslingen. Modellen anger millimeter som enhet och innehåller två modellobjekt. Utskriftsinställningar och passform behöver kontrolleras före utskrift; se [kapslingens dokumentation](hardware/enclosure/README.md).

Det finns ännu ingen byggbar firmware eller komplett monteringsanvisning i repot.

## Licens

Licens är ännu inte vald. Repot innehåller därför ingen öppen källkodslicens.
