# Hårdvara

Denna översikt bygger på projektbilderna och den bifogade firmwareversionen 1.3. Den är en inventering, inte ett verifierat kopplingsschema.

| Del | Identifiering i bilderna | Återstår att dokumentera |
| :--- | :--- | :--- |
| Styrkort | ESP8266, modul märkt ESP-12E; kort med D0–D8 och micro-USB | Exakt kortvariant och matning |
| Strömsensor | INA219 DC Current Sensor, shunt märkt R100 | Fysisk I²C-adress och faktisk inkoppling; koden använder standardkonstruktor och 32 V / 2 A-kalibrering |
| Transistor | Separat komponent märkt IRLB8721 | Funktion i kretsen och anslutningar |
| DC/DC-modul | Justerbar modul med trimpotentiometer | Exakt modell samt inställd in- och utspänning |
| Vit nätadapter | Royal BI24G-120200-AdV, utgång 12,0 V DC / 2,0 A / 24,0 W | Vilken adapter som används i den slutliga konstruktionen |
| Svart nätadapter | Linksys-adapter synlig bredvid kapslingen | Märkdata och roll i projektet |
| Kapsling | Printad låda med separat skruvat lock | Material, skruvdimensioner och monteringsmått |

## GPIO enligt koden

| Signal | Kortpinne | GPIO | Anslutning enligt källan |
| :--- | :--- | :--- | :--- |
| MOSFET-styrning | D1 | 5 | Gate via 100 Ω; gate till GND via 10 kΩ enligt kodkommentaren |
| INA219 SDA | D2 | 4 | I²C-data |
| INA219 SCL | D5 | 14 | I²C-klocka |

`setMirrorPower()` sätter D1 HIGH för matning på och LOW för av. Pinnarna används i implementationen; motståndsvärdena kommer från kodens inledande kommentar. Den faktiska inkopplingen behöver fortfarande verifieras mot bygget.

## Koppling

Fotografierna räcker inte för att fastställa ledningsdragningen. Följande behövs för ett reproducerbart bygge:

- Matningens väg genom spegel, sensor och styrkort.
- Anslutningar för INA219: VCC, GND, SDA och SCL samt VIN+ och VIN−.
- IRLB8721:s funktion, anslutningar och eventuella kringkomponenter.
- DC/DC-modulens inställda utspänning och anslutningspunkt på styrkortet.
- Kontakternas polaritet samt en komplett GPIO-tabell.

Ingen anslutning ska härledas enbart från kabelfärgerna i bilderna.

[Se komponentbilderna](gallery.md) · [Tillbaka till projektet](../README.md)
