# Firmware · MrMatzo Mirror Controller 1.3

[Öppna Arduino-sketch](esp_12V_mirror_timer/esp_12V_mirror_timer.ino)

Originalkoden är kopierad utan ändringar. Sketchen ligger i en mapp med samma namn, redo att öppnas i Arduino IDE. Beskrivningen nedan bygger på implementationen; programmet har inte kompilerats eller provkörts inom detta dokumentationsarbete.

## Byggunderlag

Koden använder ESP8266-miljön och följande inkluderade bibliotek:

| Bibliotek / header | Funktion |
| :--- | :--- |
| ESP8266WiFi, ESP8266WebServer, ESP8266mDNS | Wi-Fi, HTTP och lokalt värdnamn |
| Wire | I²C |
| LittleFS | Inställningar, sessionsnummer och logg |
| WiFiManager | Konfigurationsportal för nätverk |
| Adafruit_INA219 | Ström-, spännings- och effektmätning |

Exakt kortprofil, flashlayout, ESP8266-coreversion och biblioteksversioner från det fungerande bygget behöver anges för ett reproducerbart bygge. Webbgränssnittets HTML, CSS och JavaScript ligger inbäddade i sketchen. Serial använder 921600 baud.

## Användning

1. Vid start kopplar programmet på spegelns matning och försöker ansluta till Wi-Fi via WiFiManager.
2. Om nätverkskonfiguration behövs används portalen **Mirror-Setup**. Portalens tidsgräns är 300 sekunder; misslyckad anslutning leder till omstart.
3. Efter anslutning öppnas webbappen på [mirror.local](http://mirror.local), eller på enhetens IP-adress som skrivs till Serial.
4. Välj timerlängd, 1–1440 minuter. Förvalt värde är 10 minuter. Ändras tiden under en aktiv timer börjar nedräkningen om med den nya tiden.
5. Tänd spegelbelysningen. Bekräftad strömförbrukning startar timern när automatiken är på.

Webbappen erbjuder automatik på/av, manuell matning på/av, en tre sekunders återställning, loggvisning, CSV-nedladdning, loggrensning och återställning av Wi-Fi-inställningarna. Status hämtas varje sekund.

## Detektering och återställning

| Parameter | Värde i koden |
| :--- | :--- |
| Sensorintervall | 100 ms, när huvudloopen hinner |
| Strömfilter | Glidande medelvärde, 10 mätningar; negativa råvärden klipps till 0 |
| Tänt | Filtrerad ström ≥ 31 mA i 700 ms |
| Släckt | Filtrerad ström ≤ 26 mA i 1000 ms |
| Mellan trösklarna | Känt tillstånd behålls; okänt tillstånd förblir okänt |
| Automatiskt matningsavbrott | 3 sekunder |
| Spärr för tillståndsdetektering | 5 sekunder efter uppstart/återställd matning |
| Sensorkalibrering | `setCalibration_32V_2A()` |

Efter automatisk timeout krävs bekräftat släckt läge innan timern får starta igen. Manuell återställning och manuell påslagning har inte samma spärr: de rensar `requireOffBeforeRearm`. Sensoravläsning fortsätter under detekteringsspärren.

Timerlängden sparas över omstart. Automatikläge och pågående nedräkning sparas inte; programmet startar med automatik och matning på. Om sensorn inte hittas vid start visas sensorfel och automatisk strömdetektering uteblir. Uppstarten väntar på Wi-Fi innan normal timerhantering börjar.

## Loggning

LittleFS lagrar `/settings.txt`, `/session.txt`, `/mirror_log.csv` och den roterade `/mirror_log_old.csv`. Mätvärden loggas var tionde sekund och dessutom vid händelser. Vid storlekskontrollen från 200 KiB roteras den aktuella loggen; en tidigare roterad logg ersätts.

CSV-fält: `session,uptime_ms,raw_mA,filtered_mA,bus_V,load_V,power_mW,state,event`.

Tiden är drifttid i millisekunder, inte kalenderdatum. Fältet `load_V` beräknas i koden som busspänning plus shuntspänning; namnet återges här från programmet. Webbens nedladdning omfattar den aktuella loggen.

## Webbgränssnittets anslutningar

| Sökväg | Funktion |
| :--- | :--- |
| `/` | Webbapp |
| `/status` | Status i JSON |
| `/settime?minutes=10` | Ställ timerlängd |
| `/auto/toggle` | Växla automatik |
| `/power/on`, `/power/off` | Manuell matning |
| `/reset` | Tillfälligt avbrott i 3 sekunder |
| `/log`, `/log/download`, `/log/clear` | Visa, hämta och rensa logg |
| `/wifi-reset` | Radera Wi-Fi-inställningar och starta om |

Webbservern använder HTTP på port 80 utan inloggning i denna version. Alla som når den kan använda dess styrfunktioner.

[Tillbaka till projektet](../README.md)
