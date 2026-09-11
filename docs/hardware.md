# Hårdvara

Denna översikt bygger på märkningar och detaljer i projektbilderna. Den är en inventering, inte ett verifierat kopplingsschema.

| Del | Identifiering i bilderna | Återstår att dokumentera |
| :--- | :--- | :--- |
| Styrkort | ESP8266, modul märkt ESP-12E; kort med D0–D8 och micro-USB | Exakt kortvariant, matning och GPIO-användning |
| Strömsensor | INA219 DC Current Sensor, shunt märkt R100 | I²C-adress, kalibrering och faktisk inkoppling |
| Transistor | Separat komponent märkt IRLB8721 | Funktion i kretsen och anslutningar |
| DC/DC-modul | Justerbar modul med trimpotentiometer | Exakt modell samt inställd in- och utspänning |
| Vit nätadapter | Royal BI24G-120200-AdV, utgång 12,0 V DC / 2,0 A / 24,0 W | Vilken adapter som används i den slutliga konstruktionen |
| Svart nätadapter | Linksys-adapter synlig bredvid kapslingen | Märkdata och roll i projektet |
| Kapsling | Printad låda med separat skruvat lock | Material, skruvdimensioner och monteringsmått |

## Koppling

Fotografierna räcker inte för att fastställa ledningsdragningen. Följande behövs för ett reproducerbart bygge:

- Matningens väg genom spegel, sensor och styrkort.
- Anslutningar för INA219: VCC, GND, SDA och SCL samt VIN+ och VIN−.
- IRLB8721:s funktion, anslutningar och eventuella kringkomponenter.
- DC/DC-modulens inställda utspänning och anslutningspunkt på styrkortet.
- Kontakternas polaritet samt en komplett GPIO-tabell.

Ingen anslutning ska härledas enbart från kabelfärgerna i bilderna.

[Se komponentbilderna](gallery.md) · [Tillbaka till projektet](../README.md)
