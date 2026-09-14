/*
   ============================================================
                 MR MATZO MIRROR CONTROLLER
                        Version 1.4.0
   ============================================================

   ESP8266 + INA219 + MOSFET + LittleFS + mDNS

   Web interface:
      http://mirror.local

   D1 / GPIO5  -> 100R -> MOSFET Gate
   Gate        -> 10k -> GND

   D2 / GPIO4  -> INA219 SDA
   D5 / GPIO14 -> INA219 SCL

   Current measurements from real mirror:

      OFF:       ~20.0 - 21.5 mA
      Lowest ON: ~36.0 - 40 mA
      High ON:   hundreds of mA

   Detection:

      OFF <= 26 mA
      ON  >= 31 mA

   Dead-band between 26 and 31 mA prevents chatter.
*/


// ============================================================
// LIBRARIES
// ============================================================

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Updater.h>
#include "ShutdownRecovery.h"
#include "DeviceConfig.h"
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <LittleFS.h>


// ============================================================
// VERSION / NETWORK
// ============================================================

const char* FW_VERSION = "MrMatzo Mirror Controller 1.4.0";

const char* MDNS_HOSTNAME = "mirror";


// ============================================================
// HARDWARE
// ============================================================

const uint8_t MOSFET_PIN  = D1;
const uint8_t I2C_SDA_PIN = D2;
const uint8_t I2C_SCL_PIN = D5;

ESP8266WebServer server(80);
Adafruit_INA219 ina219;
ShutdownRecovery recovery;
bool otaInProgress = false;
bool otaSucceeded = false;
bool otaFailed = false;
bool otaAuthorized = false;
unsigned long otaLastActivity = 0;
bool fsOK = false;
bool mdnsStarted = false;
bool stationWasConnected = false;
String adminNonce;
const char* FAULT_FILE = "/shutdown_fault.txt";
const char* ADMIN_PASSWORD = MIRROR_ADMIN_PASSWORD;
static_assert(sizeof(MIRROR_ADMIN_PASSWORD) >= 17 && sizeof(MIRROR_ADMIN_PASSWORD) <= 64,
              "Choose a 16-63 character admin / Wi-Fi password");


// ============================================================
// FILE SYSTEM
// ============================================================

const char* LOG_FILE      = "/mirror_log.csv";
const char* OLD_LOG_FILE  = "/mirror_log_old.csv";
const char* SETTINGS_FILE = "/settings.txt";
const char* SESSION_FILE  = "/session.txt";

const size_t MAX_LOG_SIZE = 200UL * 1024UL;


// ============================================================
// TIMER SETTINGS
// ============================================================

unsigned long timerIntervalMs =
  10UL * 60UL * 1000UL;

const unsigned long POWER_OFF_TIME_MS =
  3000UL;

const unsigned long SENSOR_LOCKOUT_MS =
  5000UL;


// ============================================================
// CURRENT DETECTION
// ============================================================

const float CURRENT_ON_THRESHOLD_MA =
  31.0;

const float CURRENT_OFF_THRESHOLD_MA =
  26.0;

const unsigned long ON_CONFIRM_MS =
  700UL;

const unsigned long OFF_CONFIRM_MS =
  1000UL;


// ============================================================
// SENSOR
// ============================================================

const unsigned long SENSOR_INTERVAL_MS =
  100UL;

const unsigned long LOG_INTERVAL_MS =
  10000UL;

unsigned long lastSensorRead = 0;
unsigned long lastLogWrite   = 0;

float rawCurrent_mA      = 0.0;
float filteredCurrent_mA = 0.0;

float busVoltage_V       = 0.0;
float loadVoltage_V      = 0.0;
float shuntVoltage_mV    = 0.0;
float power_mW           = 0.0;


// ============================================================
// CURRENT FILTER
// ============================================================

const uint8_t FILTER_SIZE = 10;

float currentBuffer[FILTER_SIZE];

uint8_t filterIndex = 0;
uint8_t filterCount = 0;


// ============================================================
// SYSTEM STATE
// ============================================================

bool inaOK       = false;
bool powerOn     = true;
bool autoEnabled = true;

bool timerActive     = false;
bool resetInProgress = false;
bool sensorLockout   = true;

bool mirrorStateValid = false;
bool mirrorIsOn       = false;

bool requireOffBeforeRearm = false;

bool onCandidate  = false;
bool offCandidate = false;

unsigned long onCandidateStarted  = 0;
unsigned long offCandidateStarted = 0;

unsigned long timerStarted   = 0;
unsigned long resetStarted   = 0;
unsigned long lockoutStarted = 0;

uint32_t sessionNumber = 0;


// ============================================================
// JSON ESCAPE
// ============================================================

String jsonEscape(String s)
{
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\r", "");
  s.replace("\n", "\\n");

  return s;
}


// ============================================================
// USER-FACING STATE
// ============================================================

String getStateText()
{
  if (otaInProgress) return "UPPDATERAR PROGRAMVARAN";
  if (recovery.fault) return "AVSTÄNGD EFTER SLÄCKFEL";
  if (!inaOK)
    return "SENSORFEL";

  if (resetInProgress)
    return "STARTAR OM SPEGELN";

  if (!powerOn)
    return "SPEGELSTRÖM AV";

  if (sensorLockout)
    return "KONTROLLERAR SPEGELN";

  if (!mirrorStateValid)
    return "LÄSER AV SPEGELN";

  if (mirrorIsOn)
  {
    if (timerActive)
      return "SPEGEL TÄND";

    if (!autoEnabled)
      return "SPEGEL TÄND";

    if (requireOffBeforeRearm)
      return "KONTROLLERAR SLÄCKNING";

    return "SPEGEL TÄND";
  }

  return "SPEGEL SLÄCKT";
}


String getDescriptionText()
{
  if (otaInProgress) return "Matningen är av under uppdateringen. Bryt inte strömmen till styrenheten.";
  if (recovery.fault) return "Spegeln slocknade inte efter tre försök. 12 V är av. Tryck SPEGELSTRÖM PÅ för att försöka igen.";
  if (!inaOK)
    return "Strömsensorn INA219 kunde inte hittas.";

  if (resetInProgress)
    return "Strömmen är tillfälligt bruten för att återställa spegeln.";

  if (!powerOn)
    return "12 V till spegeln är manuellt frånkopplad.";

  if (sensorLockout)
    return "Systemet väntar några sekunder och läser sedan spegelns ström.";

  if (!mirrorStateValid)
    return "Bestämmer om spegelbelysningen är tänd eller släckt.";

  if (mirrorIsOn)
  {
    if (timerActive)
      return "Automatisk avstängning är aktiv.";

    if (!autoEnabled)
      return "Ljuset är tänt. Automatisk avstängning är avstängd.";

    if (requireOffBeforeRearm)
      return "Kontrollerar släckningen. Vid behov görs ett längre släckförsök.";

    return "Ljuset är tänt.";
  }

  if (autoEnabled)
    return "Timern startar automatiskt nästa gång du tänder spegeln.";

  return "Spegeln är släckt. Automatisk avstängning är avstängd.";
}


// ============================================================
// LOG FILE
// ============================================================

void createLogHeader()
{
  File f =
    LittleFS.open(
      LOG_FILE,
      "w"
    );

  if (!f)
    return;

  f.println(
    "session,uptime_ms,raw_mA,filtered_mA,"
    "bus_V,load_V,power_mW,state,event"
  );

  f.close();
}


void rotateLogIfNeeded()
{
  if (!LittleFS.exists(LOG_FILE))
    return;

  File f =
    LittleFS.open(
      LOG_FILE,
      "r"
    );

  if (!f)
    return;

  size_t fileSize =
    f.size();

  f.close();

  if (fileSize < MAX_LOG_SIZE)
    return;

  if (LittleFS.exists(OLD_LOG_FILE))
    LittleFS.remove(OLD_LOG_FILE);

  LittleFS.rename(
    LOG_FILE,
    OLD_LOG_FILE
  );

  createLogHeader();
}


void writeLog(const String& event)
{
  if (!fsOK || otaInProgress) return;
  rotateLogIfNeeded();

  File f =
    LittleFS.open(
      LOG_FILE,
      "a"
    );

  if (!f)
    return;

  f.print(sessionNumber);
  f.print(",");

  f.print(millis());
  f.print(",");

  f.print(rawCurrent_mA, 2);
  f.print(",");

  f.print(filteredCurrent_mA, 2);
  f.print(",");

  f.print(busVoltage_V, 3);
  f.print(",");

  f.print(loadVoltage_V, 3);
  f.print(",");

  f.print(power_mW, 1);
  f.print(",");

  f.print(getStateText());
  f.print(",");

  f.println(event);

  f.close();
}


void logEvent(const String& text)
{
  Serial.print("[EVENT] ");
  Serial.println(text);

  writeLog(text);
}


// ============================================================
// SESSION
// ============================================================

void loadSession()
{
  sessionNumber = 0;

  if (LittleFS.exists(SESSION_FILE))
  {
    File f =
      LittleFS.open(
        SESSION_FILE,
        "r"
      );

    if (f)
    {
      sessionNumber =
        f.readString().toInt();

      f.close();
    }
  }

  sessionNumber++;

  File f =
    LittleFS.open(
      SESSION_FILE,
      "w"
    );

  if (f)
  {
    f.print(sessionNumber);
    f.close();
  }
}


// ============================================================
// SETTINGS
// ============================================================

void saveSettings()
{
  File f =
    LittleFS.open(
      SETTINGS_FILE,
      "w"
    );

  if (!f)
    return;

  f.print(
    timerIntervalMs /
    60000UL
  );

  f.close();
}


void loadSettings()
{
  if (!LittleFS.exists(SETTINGS_FILE))
    return;

  File f =
    LittleFS.open(
      SETTINGS_FILE,
      "r"
    );

  if (!f)
    return;

  String line =
    f.readString();

  f.close();

  long minutes =
    line.toInt();

  if (
    minutes >= 1 &&
    minutes <= 1440
  )
  {
    timerIntervalMs =
      (unsigned long)minutes *
      60000UL;
  }
}


// ============================================================
// MOSFET
// ============================================================

void setMirrorPower(bool on)
{
  powerOn = on;

  digitalWrite(
    MOSFET_PIN,
    on ? HIGH : LOW
  );

  Serial.print("Mirror 12V: ");

  Serial.println(
    on ? "ON" : "OFF"
  );
}


// ============================================================
// CURRENT FILTER
// ============================================================

void clearCurrentFilter()
{
  for (
    uint8_t i = 0;
    i < FILTER_SIZE;
    i++
  )
  {
    currentBuffer[i] = 0;
  }

  filterIndex = 0;
  filterCount = 0;

  rawCurrent_mA      = 0;
  filteredCurrent_mA = 0;
}


void addCurrentSample(float value)
{
  currentBuffer[filterIndex] =
    value;

  filterIndex++;

  if (filterIndex >= FILTER_SIZE)
    filterIndex = 0;

  if (filterCount < FILTER_SIZE)
    filterCount++;

  float total = 0;

  for (
    uint8_t i = 0;
    i < filterCount;
    i++
  )
  {
    total += currentBuffer[i];
  }

  if (filterCount > 0)
  {
    filteredCurrent_mA =
      total /
      filterCount;
  }
}


// ============================================================
// INA219
// ============================================================

void readINA219()
{
  if (!inaOK)
    return;

  if (
    millis() -
    lastSensorRead <
    SENSOR_INTERVAL_MS
  )
  {
    return;
  }

  lastSensorRead =
    millis();

  shuntVoltage_mV =
    ina219.getShuntVoltage_mV();

  busVoltage_V =
    ina219.getBusVoltage_V();

  rawCurrent_mA =
    ina219.getCurrent_mA();

  power_mW =
    ina219.getPower_mW();

  loadVoltage_V =
    busVoltage_V +
    (
      shuntVoltage_mV /
      1000.0
    );

  if (rawCurrent_mA < 0)
    rawCurrent_mA = 0;

  addCurrentSample(
    rawCurrent_mA
  );
}


// ============================================================
// TIMER
// ============================================================

void startTimer()
{
  if (!autoEnabled)
    return;

  if (!mirrorIsOn)
    return;

  if (requireOffBeforeRearm)
    return;

  timerActive =
    true;

  timerStarted =
    millis();

  logEvent(
    "TIMER_STARTED"
  );
}


void stopTimer(
  const String& reason
)
{
  if (!timerActive)
    return;

  timerActive =
    false;

  logEvent(
    "TIMER_STOPPED_" +
    reason
  );
}


unsigned long getRemainingMs()
{
  if (!timerActive)
    return 0;

  unsigned long elapsed =
    millis() -
    timerStarted;

  if (elapsed >= timerIntervalMs)
    return 0;

  return
    timerIntervalMs -
    elapsed;
}


void beginPowerCycle()
{
  recovery.beginAttempt();
  timerActive =
    false;

  resetInProgress =
    true;

  resetStarted =
    millis();

  requireOffBeforeRearm =
    true;

  mirrorStateValid =
    false;

  mirrorIsOn =
    false;

  onCandidate =
    false;

  offCandidate =
    false;

  setMirrorPower(
    false
  );

  logEvent(
    recovery.attempts == 1 ? "TIMER_EXPIRED_POWER_OFF" :
    "SHUTDOWN_RETRY_" + String(recovery.attempts)
  );
}


void processTimer()
{
  if (!timerActive)
    return;

  if (
    millis() -
    timerStarted >=
    timerIntervalMs
  )
  {
    beginPowerCycle();
  }
}


// ============================================================
// POWER CYCLE
// ============================================================

void processPowerCycle()
{
  if (otaInProgress || recovery.fault) return;
  if (!resetInProgress)
    return;

  if (
    millis() -
    resetStarted <
    (recovery.pending ? recovery.offTimeMs() : POWER_OFF_TIME_MS)
  )
  {
    return;
  }

  resetInProgress =
    false;

  setMirrorPower(
    true
  );

  clearCurrentFilter();

  sensorLockout =
    true;

  lockoutStarted =
    millis();

  mirrorStateValid =
    false;

  mirrorIsOn =
    false;

  onCandidate =
    false;

  offCandidate =
    false;

  recovery.restored(millis());

  logEvent(
    "POWER_RESTORED"
  );
}


// ============================================================
// SENSOR LOCKOUT
// ============================================================

void processSensorLockout()
{
  if (!sensorLockout)
    return;

  if (
    millis() -
    lockoutStarted <
    SENSOR_LOCKOUT_MS
  )
  {
    return;
  }

  sensorLockout =
    false;

  mirrorStateValid =
    false;

  onCandidate =
    false;

  offCandidate =
    false;

  logEvent(
    "SENSOR_READY"
  );
}


// ============================================================
// MIRROR STATE
// ============================================================

void confirmedMirrorOff()
{
  if (recovery.pending) logEvent("SHUTDOWN_CONFIRMED_OFF");
  recovery.clear();
  bool wasOn =
    mirrorStateValid &&
    mirrorIsOn;

  mirrorStateValid =
    true;

  mirrorIsOn =
    false;

  onCandidate =
    false;

  offCandidate =
    false;

  requireOffBeforeRearm =
    false;

  if (timerActive)
  {
    stopTimer(
      "MIRROR_OFF"
    );
  }

  if (wasOn)
  {
    logEvent(
      "MIRROR_LIGHT_OFF"
    );
  }
  else
  {
    logEvent(
      "MIRROR_CONFIRMED_OFF"
    );
  }
}


void confirmedMirrorOn()
{
  bool wasOff =
    !mirrorStateValid ||
    !mirrorIsOn;

  mirrorStateValid =
    true;

  mirrorIsOn =
    true;

  onCandidate =
    false;

  offCandidate =
    false;

  if (wasOff)
  {
    logEvent(
      "MIRROR_LIGHT_ON"
    );
  }

  if (
    autoEnabled &&
    !timerActive &&
    !requireOffBeforeRearm
  )
  {
    startTimer();
  }
}


// ============================================================
// MIRROR DETECTION
// ============================================================

void processMirrorDetection()
{
  if (!inaOK)
    return;

  if (!powerOn)
    return;

  if (resetInProgress)
    return;

  if (sensorLockout)
    return;


  // ----------------------------------------------------------
  // UNKNOWN STATE
  // ----------------------------------------------------------

  if (!mirrorStateValid)
  {
    if (
      filteredCurrent_mA <=
      CURRENT_OFF_THRESHOLD_MA
    )
    {
      onCandidate =
        false;

      if (!offCandidate)
      {
        offCandidate =
          true;

        offCandidateStarted =
          millis();
      }

      if (
        millis() -
        offCandidateStarted >=
        OFF_CONFIRM_MS
      )
      {
        confirmedMirrorOff();
      }
    }

    else if (
      filteredCurrent_mA >=
      CURRENT_ON_THRESHOLD_MA
    )
    {
      offCandidate =
        false;

      if (!onCandidate)
      {
        onCandidate =
          true;

        onCandidateStarted =
          millis();
      }

      if (
        millis() -
        onCandidateStarted >=
        ON_CONFIRM_MS
      )
      {
        confirmedMirrorOn();
      }
    }

    else
    {
      onCandidate =
        false;

      offCandidate =
        false;
    }

    return;
  }


  // ----------------------------------------------------------
  // MIRROR CURRENTLY ON
  // ----------------------------------------------------------

  if (mirrorIsOn)
  {
    if (
      filteredCurrent_mA <=
      CURRENT_OFF_THRESHOLD_MA
    )
    {
      if (!offCandidate)
      {
        offCandidate =
          true;

        offCandidateStarted =
          millis();
      }

      if (
        millis() -
        offCandidateStarted >=
        OFF_CONFIRM_MS
      )
      {
        confirmedMirrorOff();
      }
    }
    else
    {
      offCandidate =
        false;
    }

    return;
  }


  // ----------------------------------------------------------
  // MIRROR CURRENTLY OFF
  // ----------------------------------------------------------

  if (
    filteredCurrent_mA >=
    CURRENT_ON_THRESHOLD_MA
  )
  {
    if (!onCandidate)
    {
      onCandidate =
        true;

      onCandidateStarted =
        millis();
    }

    if (
      millis() -
      onCandidateStarted >=
      ON_CONFIRM_MS
    )
    {
      confirmedMirrorOn();
    }
  }
  else
  {
    onCandidate =
      false;
  }
}


// ============================================================
// PERIODIC LOGGING
// ============================================================

void processLogging()
{
  if (
    millis() -
    lastLogWrite <
    LOG_INTERVAL_MS
  )
  {
    return;
  }

  lastLogWrite =
    millis();

  writeLog("-");
}


// ============================================================
// WEB PAGE
// ============================================================

const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sv">

<head>

<meta charset="UTF-8">

<meta
  name="viewport"
  content="width=device-width,initial-scale=1,user-scalable=no">

<meta
  name="theme-color"
  content="#090b10">

<title>MrMatzo Mirror</title>

<style>

:root
{
  --bg:#090b10;
  --panel:#111620;
  --panel2:#171d28;
  --line:rgba(255,255,255,.07);

  --text:#f4f4f5;
  --muted:#7f8998;

  --gold:#f5bb42;
  --gold2:#d98c20;

  --green:#46d693;
  --red:#ff6273;
  --blue:#57a8ff;
}

*
{
  box-sizing:border-box;
}

body
{
  margin:0;

  background:
    radial-gradient(
      circle at 50% -120px,
      #313b52 0,
      #11151e 35%,
      #080a0e 75%
    );

  min-height:100vh;

  color:var(--text);

  font-family:
    -apple-system,
    BlinkMacSystemFont,
    "Segoe UI",
    sans-serif;

  padding:8px;
}

.app
{
  max-width:430px;
  margin:0 auto;
}

header
{
  display:flex;
  justify-content:space-between;
  align-items:center;

  margin:2px 4px 7px;
}

.brand
{
  font-size:19px;
  font-weight:900;
  letter-spacing:2.5px;
}

.brandSmall
{
  margin-top:1px;

  color:var(--muted);

  font-size:8px;
  letter-spacing:2.4px;
}

.connection
{
  text-align:right;

  font-size:9px;

  color:var(--green);

  font-weight:700;

  letter-spacing:.6px;
}

.card
{
  border:1px solid var(--line);

  background:
    linear-gradient(
      145deg,
      rgba(24,30,42,.97),
      rgba(13,17,24,.98)
    );

  border-radius:15px;

  box-shadow:
    0 12px 28px rgba(0,0,0,.28);

  padding:10px;

  margin-bottom:7px;
}


/* ==========================================================
   MAIN STATUS
   ========================================================== */

.main
{
  display:grid;

  grid-template-columns:156px 1fr;

  gap:10px;

  align-items:center;
}

.dial
{
  position:relative;

  width:150px;
  height:150px;

  margin:auto;
}

.dial svg
{
  width:150px;
  height:150px;

  transform:rotate(-90deg);
}

.ringBackground
{
  fill:none;

  stroke:#29303b;

  stroke-width:9;
}

.ring
{
  fill:none;

  stroke:url(#goldGradient);

  stroke-width:9;

  stroke-linecap:round;

  stroke-dasharray:402.12;
  stroke-dashoffset:402.12;

  filter:
    drop-shadow(
      0 0 5px
      rgba(245,187,66,.55)
    );
}

.dialCenter
{
  position:absolute;

  inset:0;

  display:flex;

  flex-direction:column;

  align-items:center;
  justify-content:center;

  text-align:center;
}

.countdown
{
  font-size:29px;

  font-weight:350;

  font-variant-numeric:
    tabular-nums;

  letter-spacing:-1px;
}

.countdown.ready
{
  font-size:21px;

  font-weight:800;

  letter-spacing:.4px;
}

.dialLabel
{
  margin-top:5px;

  max-width:105px;

  color:var(--muted);

  font-size:7px;

  font-weight:700;

  letter-spacing:1.1px;

  line-height:1.35;
}

.statusTitle
{
  font-size:14px;

  font-weight:850;

  line-height:1.15;
}

.statusDescription
{
  color:var(--muted);

  margin-top:5px;

  font-size:9px;

  line-height:1.35;

  min-height:25px;
}

.currentRow
{
  display:flex;

  align-items:baseline;

  margin-top:10px;
}

.current
{
  font-size:30px;

  font-weight:850;

  font-variant-numeric:
    tabular-nums;
}

.currentUnit
{
  color:var(--muted);

  font-size:10px;

  margin-left:3px;
}

.metrics
{
  display:grid;

  grid-template-columns:
    repeat(3,1fr);

  gap:4px;

  margin-top:7px;
}

.metric
{
  text-align:center;

  background:
    rgba(255,255,255,.035);

  padding:5px 2px;

  border-radius:8px;
}

.metricValue
{
  font-size:11px;

  font-weight:750;
}

.metricLabel
{
  color:var(--muted);

  font-size:6px;

  letter-spacing:.8px;

  margin-top:2px;
}


/* ==========================================================
   TIMER SETTING
   ========================================================== */

.sectionTitle
{
  color:var(--muted);

  font-size:8px;

  font-weight:800;

  letter-spacing:1.2px;

  margin-bottom:7px;
}

.timeEditor
{
  display:grid;

  grid-template-columns:
    42px 1fr 42px 105px;

  gap:5px;
}

button
{
  border:0;

  border-radius:9px;

  min-height:35px;

  color:white;

  background:#29313e;

  font-size:10px;

  font-weight:800;

  cursor:pointer;
}

button:active
{
  transform:scale(.97);
}

.minus,
.plus
{
  font-size:19px;
}

.save
{
  background:
    linear-gradient(
      135deg,
      #f4c24f,
      #ce851d
    );

  color:#191208;
}

input[type=number]
{
  width:100%;

  min-width:0;

  border:1px solid var(--line);

  outline:none;

  border-radius:9px;

  background:#0b0e14;

  color:white;

  text-align:center;

  font-size:17px;

  font-weight:800;
}

.timeHint
{
  color:var(--muted);

  text-align:center;

  margin-top:5px;

  font-size:8px;
}


/* ==========================================================
   AUTO
   ========================================================== */

.autoRow
{
  display:grid;

  grid-template-columns:1fr 92px;

  gap:8px;

  align-items:center;
}

.autoTitle
{
  font-size:12px;

  font-weight:800;
}

.autoText
{
  color:var(--muted);

  font-size:8px;

  line-height:1.3;

  margin-top:2px;
}

.autoButton.on
{
  background:#197550;
}

.autoButton.off
{
  background:#73313b;
}


/* ==========================================================
   CONTROLS
   ========================================================== */

.controls
{
  display:grid;

  grid-template-columns:
    repeat(3,1fr);

  gap:5px;
}

.controls button
{
  font-size:8px;
}

.powerOn
{
  background:#176546;
}

.powerOff
{
  background:#74313b;
}

.reset
{
  background:#72591e;
}


/* ==========================================================
   BOTTOM
   ========================================================== */

.tools
{
  display:grid;

  grid-template-columns:
    repeat(4,1fr);

  gap:4px;
}

.tools button
{
  min-height:31px;

  font-size:8px;
}

.network
{
  margin-top:7px;

  display:flex;

  justify-content:space-between;

  gap:8px;

  color:var(--muted);

  font-size:8px;
}

.mdns
{
  margin-top:5px;

  text-align:center;

  color:var(--gold);

  font-size:8px;

  font-weight:700;
}

.toast
{
  position:fixed;

  left:50%;
  bottom:20px;

  transform:
    translateX(-50%)
    translateY(20px);

  padding:9px 15px;

  border-radius:20px;

  background:#f5bb42;

  color:#171109;

  font-size:10px;

  font-weight:850;

  opacity:0;

  pointer-events:none;

  transition:.22s;
}

.toast.show
{
  opacity:1;

  transform:
    translateX(-50%)
    translateY(0);
}

@media(max-width:360px)
{
  .main
  {
    grid-template-columns:
      140px 1fr;
  }

  .dial,
  .dial svg
  {
    width:136px;
    height:136px;
  }

  .timeEditor
  {
    grid-template-columns:
      38px 1fr 38px 90px;
  }
}

</style>

</head>


<body>

<div class="app">


<header>

  <div>

    <div class="brand">
      MR MATZO
    </div>

    <div class="brandSmall">
      MIRROR CONTROLLER
    </div>

  </div>


  <div
    id="connection"
    class="connection">

    ANSLUTER

  </div>

</header>



<div class="card main">


  <div class="dial">


    <svg viewBox="0 0 150 150">

      <defs>

        <linearGradient
          id="goldGradient"
          x1="0"
          y1="0"
          x2="1"
          y2="1">

          <stop
            offset="0%"
            stop-color="#fff2ad"/>

          <stop
            offset="48%"
            stop-color="#f5bb42"/>

          <stop
            offset="100%"
            stop-color="#cf7d17"/>

        </linearGradient>

      </defs>


      <circle
        class="ringBackground"
        cx="75"
        cy="75"
        r="64"/>


      <circle
        id="ring"
        class="ring"
        cx="75"
        cy="75"
        r="64"/>

    </svg>


    <div class="dialCenter">

      <div
        id="countdown"
        class="countdown ready">

        REDO

      </div>


      <div
        id="dialLabel"
        class="dialLabel">

        SPEGEL SLÄCKT

      </div>

    </div>

  </div>



  <div>


    <div
      id="statusTitle"
      class="statusTitle">

      Läser av...

    </div>


    <div
      id="statusDescription"
      class="statusDescription">

      Ansluter till styrenheten.

    </div>


    <div class="currentRow">

      <span
        id="current"
        class="current">

        0.0

      </span>

      <span class="currentUnit">
        mA
      </span>

    </div>


    <div class="metrics">


      <div class="metric">

        <div
          id="voltage"
          class="metricValue">

          --

        </div>

        <div class="metricLabel">
          VOLT
        </div>

      </div>


      <div class="metric">

        <div
          id="power"
          class="metricValue">

          --

        </div>

        <div class="metricLabel">
          WATT
        </div>

      </div>


      <div class="metric">

        <div
          id="lightState"
          class="metricValue">

          --

        </div>

        <div class="metricLabel">
          LJUS
        </div>

      </div>


    </div>

  </div>

</div>



<div class="card">

  <div class="sectionTitle">
    AUTOMATISK AVSTÄNGNING EFTER
  </div>


  <div class="timeEditor">


    <button
      class="minus"
      onclick="changeMinutes(-1)">

      −

    </button>


    <input
      id="minutesInput"
      type="number"
      min="1"
      max="1440"
      value="10">


    <button
      class="plus"
      onclick="changeMinutes(1)">

      +

    </button>


    <button
      class="save"
      onclick="saveMinutes()">

      SPARA TID

    </button>


  </div>


  <div
    id="timeHint"
    class="timeHint">

    Inställd tid: 10 minuter

  </div>

</div>



<div class="card">

  <div class="autoRow">


    <div>

      <div class="autoTitle">
        Automatisk avstängning
      </div>


      <div
        id="autoDescription"
        class="autoText">

        När spegeln tänds startar timern automatiskt.

      </div>

    </div>


    <button
      id="autoButton"
      class="autoButton on"
      onclick="toggleAuto()">

      PÅ

    </button>


  </div>

</div>



<div class="card">

  <div class="sectionTitle">
    SERVICE
  </div>


  <div class="controls">

    <button
      class="powerOn"
      onclick="powerOn()">

      SPEGELSTRÖM PÅ

    </button>


    <button
      class="powerOff"
      onclick="powerOff()">

      SPEGELSTRÖM AV

    </button>


    <button
      class="reset"
      onclick="resetMirror()">

      STARTA OM SPEGEL

    </button>

  </div>

</div>



<div class="card">


  <div class="tools">


    <button
      onclick="location.href='/log'">

      VISA LOGG

    </button>
    <button onclick="location.href='/update'">UPPDATERA</button>
    <button onclick="location.href='/log/old/download'">FÖREGÅENDE LOGG</button>


    <button
      onclick="location.href='/log/download'">

      CSV

    </button>


    <button
      onclick="clearLog()">

      RENSA LOGG

    </button>


    <button
      onclick="location.href='/wifi'">

      WIFI

    </button>


  </div>


  <div class="network">

    <span>

      <span id="ssid">
        --
      </span>

      ·

      <span id="rssi">
        --
      </span>

      dBm

    </span>


    <span id="ip">
      --
    </span>

  </div>


  <div class="mdns">
    mirror.local
  </div>


</div>


</div>



<div
  id="toast"
  class="toast">
</div>



<script>

// ==========================================================
// CONSTANTS
// ==========================================================

const ring =
  document.getElementById("ring");

const circumference =
  2 * Math.PI * 64;

ring.style.strokeDasharray =
  circumference;


let timerSnapshotRemaining = 0;
let timerSnapshotTotal     = 0;
let timerSnapshotAt        = performance.now();
let timerIsRunning         = false;

let mirrorOn       = false;
let mirrorValid    = false;
let autoEnabled    = true;
let resetRunning   = false;
let mirrorPowerOn  = true;

let serverMinutes = 10;

let timeDirty = false;


const minutesInput =
  document.getElementById(
    "minutesInput"
  );


minutesInput.addEventListener(
  "input",
  () =>
  {
    timeDirty = true;
  }
);


// ==========================================================
// MINUTES
// ==========================================================

function clampMinutes(value)
{
  value =
    parseInt(value);

  if (isNaN(value))
    value = 1;

  if (value < 1)
    value = 1;

  if (value > 1440)
    value = 1440;

  return value;
}


function changeMinutes(delta)
{
  let value =
    clampMinutes(
      minutesInput.value
    );

  value =
    clampMinutes(
      value + delta
    );

  minutesInput.value =
    value;

  timeDirty =
    true;
}


async function saveMinutes()
{
  const value =
    clampMinutes(
      minutesInput.value
    );

  minutesInput.value =
    value;

  try
  {
    const response =
      await fetch(
        "/settime?minutes=" +
        value,
        {
          cache:"no-store"
        }
      );

    if (!response.ok)
      throw new Error();

    serverMinutes =
      value;

    timeDirty =
      false;

    showToast(
      "Sparat: " +
      value +
      " min"
    );

    await updateStatus();
  }
  catch(e)
  {
    showToast(
      "Kunde inte spara tiden"
    );
  }
}


// ==========================================================
// TIMER FORMAT
// ==========================================================

function formatTime(ms)
{
  if (ms < 0)
    ms = 0;

  const totalSeconds =
    Math.ceil(
      ms / 1000
    );

  const h =
    Math.floor(
      totalSeconds /
      3600
    );

  const m =
    Math.floor(
      (
        totalSeconds %
        3600
      ) /
      60
    );

  const s =
    totalSeconds %
    60;

  if (h > 0)
  {
    return (
      String(h).padStart(2,"0")
      + ":"
      + String(m).padStart(2,"0")
      + ":"
      + String(s).padStart(2,"0")
    );
  }

  return (
    String(m).padStart(2,"0")
    + ":"
    + String(s).padStart(2,"0")
  );
}


// ==========================================================
// SMOOTH TIMER
// ==========================================================

function animate()
{
  const now =
    performance.now();

  let remaining =
    timerSnapshotRemaining;

  if (timerIsRunning)
  {
    remaining -=
      now -
      timerSnapshotAt;

    if (remaining < 0)
      remaining = 0;
  }

  const countdown =
    document.getElementById(
      "countdown"
    );

  const dialLabel =
    document.getElementById(
      "dialLabel"
    );


  if (resetRunning)
  {
    countdown.innerText =
      "RESET";

    countdown.className =
      "countdown ready";

    dialLabel.innerText =
      "STARTAR OM";

    ring.style.strokeDashoffset =
      0;

    ring.style.opacity =
      .55;
  }

  else if (
    timerIsRunning &&
    timerSnapshotTotal > 0
  )
  {
    countdown.innerText =
      formatTime(
        remaining
      );

    countdown.className =
      "countdown";

    dialLabel.innerText =
      "TID KVAR";

    let ratio =
      remaining /
      timerSnapshotTotal;

    ratio =
      Math.max(
        0,
        Math.min(
          1,
          ratio
        )
      );

    ring.style.strokeDashoffset =
      circumference *
      (1 - ratio);

    ring.style.opacity =
      1;
  }

  else if (
    mirrorValid &&
    mirrorOn
  )
  {
    countdown.innerText =
      "TÄND";

    countdown.className =
      "countdown ready";

    dialLabel.innerText =
      autoEnabled
      ? "SPEGEL TÄND"
      : "AUTOMATIK AV";

    ring.style.strokeDashoffset =
      0;

    ring.style.opacity =
      1;
  }

  else
  {
    countdown.innerText =
      "REDO";

    countdown.className =
      "countdown ready";

    dialLabel.innerText =
      mirrorPowerOn
      ? "SPEGEL SLÄCKT"
      : "STRÖM AV";

    ring.style.strokeDashoffset =
      circumference;

    ring.style.opacity =
      .25;
  }

  requestAnimationFrame(
    animate
  );
}


// ==========================================================
// STATUS
// ==========================================================

async function updateStatus()
{
  try
  {
    const response =
      await fetch(
        "/status",
        {
          cache:"no-store"
        }
      );

    if (!response.ok)
      throw new Error();

    const data =
      await response.json();


    document.getElementById(
      "connection"
    ).innerText =
      "ANSLUTEN";


    document.getElementById(
      "statusTitle"
    ).innerText =
      data.state;


    document.getElementById(
      "statusDescription"
    ).innerText =
      data.description;


    document.getElementById(
      "current"
    ).innerText =
      data.current.toFixed(1);


    document.getElementById(
      "voltage"
    ).innerText =
      data.loadVoltage.toFixed(2);


    document.getElementById(
      "power"
    ).innerText =
      (
        data.power /
        1000
      ).toFixed(2);


    document.getElementById(
      "ssid"
    ).innerText =
      data.ssid;


    document.getElementById(
      "rssi"
    ).innerText =
      data.rssi;


    document.getElementById(
      "ip"
    ).innerText =
      data.ip;


    mirrorOn =
      data.mirrorOn;

    mirrorValid =
      data.mirrorStateValid;

    autoEnabled =
      data.autoEnabled;

    resetRunning =
      data.resetInProgress;

    mirrorPowerOn =
      data.powerOn;

    timerIsRunning =
      data.timerActive;


    timerSnapshotRemaining =
      data.remainingMs;

    timerSnapshotTotal =
      data.intervalMs;

    timerSnapshotAt =
      performance.now();


    document.getElementById(
      "lightState"
    ).innerText =
      !mirrorValid
      ? "..."
      : (
          mirrorOn
          ? "TÄND"
          : "AV"
        );


    const autoButton =
      document.getElementById(
        "autoButton"
      );


    if (autoEnabled)
    {
      autoButton.innerText =
        "PÅ";

      autoButton.className =
        "autoButton on";

      document.getElementById(
        "autoDescription"
      ).innerText =
        "När spegeln tänds startar nedräkningen automatiskt.";
    }
    else
    {
      autoButton.innerText =
        "AV";

      autoButton.className =
        "autoButton off";

      document.getElementById(
        "autoDescription"
      ).innerText =
        "Spegeln fungerar normalt men ingen automatisk timer startar.";
    }


    serverMinutes =
      data.intervalMinutes;


    document.getElementById(
      "timeHint"
    ).innerText =
      "Inställd tid: " +
      serverMinutes +
      (
        serverMinutes === 1
        ? " minut"
        : " minuter"
      );


    if (!timeDirty)
    {
      minutesInput.value =
        serverMinutes;
    }
  }
  catch(e)
  {
    document.getElementById(
      "connection"
    ).innerText =
      "OFFLINE";
  }
}


// ==========================================================
// AUTO
// ==========================================================

async function toggleAuto()
{
  try
  {
    await fetch(
      "/auto/toggle",
      {
        cache:"no-store"
      }
    );

    await updateStatus();
  }
  catch(e)
  {
    showToast(
      "Ingen kontakt"
    );
  }
}


// ==========================================================
// POWER
// ==========================================================

async function powerOn()
{
  await fetch(
    "/power/on",
    {
      cache:"no-store"
    }
  );

  showToast(
    "Spegelström på"
  );

  updateStatus();
}


async function powerOff()
{
  if (
    !confirm(
      "Bryta 12 V till spegeln?"
    )
  )
  {
    return;
  }

  await fetch(
    "/power/off",
    {
      cache:"no-store"
    }
  );

  showToast(
    "Spegelström av"
  );

  updateStatus();
}


async function resetMirror()
{
  if (
    !confirm(
      "Bryta strömmen i 3 sekunder och starta om spegeln?"
    )
  )
  {
    return;
  }

  await fetch(
    "/reset",
    {
      cache:"no-store"
    }
  );

  updateStatus();
}


// ==========================================================
// LOG
// ==========================================================

async function clearLog()
{
  if (
    !confirm(
      "Rensa diagnostic-loggen?"
    )
  )
  {
    return;
  }

  await fetch(
    "/log/clear",
    {
      cache:"no-store"
    }
  );

  showToast(
    "Loggen är rensad"
  );
}


// ==========================================================
// WIFI
// ==========================================================

async function resetWiFi()
{
  if (
    !confirm(
      "Radera sparat Wi-Fi och starta om?"
    )
  )
  {
    return;
  }

  await fetch(
    "/wifi-reset",
    {
      cache:"no-store"
    }
  );
}


// ==========================================================
// TOAST
// ==========================================================

let toastTimer = 0;


function showToast(text)
{
  const toast =
    document.getElementById(
      "toast"
    );

  toast.innerText =
    text;

  toast.classList.add(
    "show"
  );

  clearTimeout(
    toastTimer
  );

  toastTimer =
    setTimeout(
      () =>
      {
        toast.classList.remove(
          "show"
        );
      },
      1800
    );
}


// ==========================================================
// START WEB UI
// ==========================================================

updateStatus();

setInterval(
  updateStatus,
  1000
);

requestAnimationFrame(
  animate
);

</script>

</body>
</html>
)rawliteral";


// ============================================================
// WEB ROOT
// ============================================================

void handleRoot()
{
  server.send_P(
    200,
    "text/html",
    MAIN_PAGE
  );
}


// ============================================================
// STATUS JSON
// ============================================================

void handleStatus()
{
  String json = "{";
  json += "\"shutdownFault\":" + String(recovery.fault ? "true" : "false");
  json += ",\"shutdownAttempt\":" + String(recovery.attempts);
  json += ",\"otaInProgress\":" + String(otaInProgress ? "true" : "false");
  json += ",\"apIP\":\"" + WiFi.softAPIP().toString() + "\",";


  json += "\"sensorOK\":";
  json +=
    inaOK
    ? "true"
    : "false";


  json += ",\"powerOn\":";
  json +=
    powerOn
    ? "true"
    : "false";


  json += ",\"autoEnabled\":";
  json +=
    autoEnabled
    ? "true"
    : "false";


  json += ",\"timerActive\":";
  json +=
    timerActive
    ? "true"
    : "false";


  json += ",\"resetInProgress\":";
  json +=
    resetInProgress
    ? "true"
    : "false";


  json += ",\"mirrorStateValid\":";
  json +=
    mirrorStateValid
    ? "true"
    : "false";


  json += ",\"mirrorOn\":";
  json +=
    mirrorIsOn
    ? "true"
    : "false";


  json += ",\"requireOffBeforeRearm\":";
  json +=
    requireOffBeforeRearm
    ? "true"
    : "false";


  json += ",\"current\":";
  json += String(
    filteredCurrent_mA,
    2
  );


  json += ",\"rawCurrent\":";
  json += String(
    rawCurrent_mA,
    2
  );


  json += ",\"loadVoltage\":";
  json += String(
    loadVoltage_V,
    3
  );


  json += ",\"busVoltage\":";
  json += String(
    busVoltage_V,
    3
  );


  json += ",\"power\":";
  json += String(
    power_mW,
    1
  );


  json += ",\"remainingMs\":";
  json += String(
    getRemainingMs()
  );


  json += ",\"intervalMs\":";
  json += String(
    timerIntervalMs
  );


  json += ",\"intervalMinutes\":";
  json += String(
    timerIntervalMs /
    60000UL
  );


  json += ",\"state\":\"";
  json += jsonEscape(
    getStateText()
  );
  json += "\"";


  json += ",\"description\":\"";
  json += jsonEscape(
    getDescriptionText()
  );
  json += "\"";


  json += ",\"ssid\":\"";
  json += jsonEscape(
    WiFi.SSID()
  );
  json += "\"";


  json += ",\"ip\":\"";
  json +=
    WiFi.localIP().toString();
  json += "\"";


  json += ",\"hostname\":\"mirror.local\"";


  json += ",\"rssi\":";
  json += String(
    WiFi.RSSI()
  );


  json += ",\"session\":";
  json += String(
    sessionNumber
  );


  json += ",\"firmware\":\"";
  json += FW_VERSION;
  json += "\"";


  json += "}";


  server.send(
    200,
    "application/json",
    json
  );
}


// ============================================================
// LOG VIEW
// ============================================================

void handleLogView()
{
  if (!LittleFS.exists(LOG_FILE))
  {
    server.send(
      200,
      "text/plain",
      "No log available"
    );

    return;
  }


  File f =
    LittleFS.open(
      LOG_FILE,
      "r"
    );


  server.setContentLength(
    CONTENT_LENGTH_UNKNOWN
  );


  server.send(
    200,
    "text/html",
    ""
  );


  server.sendContent(
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{background:#080a0e;color:#ddd;"
    "font-family:monospace;padding:14px}"
    "a{color:#f5bb42;margin-right:15px}"
    "pre{font-size:10px;white-space:pre-wrap}"
    "</style>"
    "</head>"
    "<body>"
    "<h3>MR MATZO DIAGNOSTIC LOG</h3>"
    "<a href='/'>TILLBAKA</a>"
    "<a href='/log/download'>LADDA NER CSV</a>"
    "<pre>"
  );


  char buffer[512];


  while (f.available())
  {
    size_t n =
      f.readBytes(
        buffer,
        sizeof(buffer)-1
      );

    buffer[n] = 0;


    String text(
      buffer
    );

    text.replace(
      "&",
      "&amp;"
    );

    text.replace(
      "<",
      "&lt;"
    );

    text.replace(
      ">",
      "&gt;"
    );


    server.sendContent(
      text
    );


    yield();
  }


  f.close();


  server.sendContent(
    "</pre></body></html>"
  );
}


// ============================================================
// LOG DOWNLOAD
// ============================================================

void handleLogDownload()
{
  if (!LittleFS.exists(LOG_FILE))
  {
    server.send(
      404,
      "text/plain",
      "No log"
    );

    return;
  }


  File f =
    LittleFS.open(
      LOG_FILE,
      "r"
    );


  server.sendHeader(
    "Content-Disposition",
    "attachment; filename=mrmatzo_mirror_log.csv"
  );


  server.streamFile(
    f,
    "text/csv"
  );


  f.close();
}


// ============================================================
// CLEAR LOG
// ============================================================

void clearLog()
{
  if (LittleFS.exists(LOG_FILE))
    LittleFS.remove(LOG_FILE);


  if (LittleFS.exists(OLD_LOG_FILE))
    LittleFS.remove(OLD_LOG_FILE);


  createLogHeader();


  logEvent(
    "LOG_CLEARED"
  );
}


// ============================================================
// MANUAL POWER ON
// ============================================================

void manualPowerOn()
{
  recovery.clear();
  if (fsOK) LittleFS.remove(FAULT_FILE);
  timerActive =
    false;

  resetInProgress =
    false;

  requireOffBeforeRearm =
    false;


  setMirrorPower(
    true
  );


  clearCurrentFilter();


  sensorLockout =
    true;

  lockoutStarted =
    millis();


  mirrorStateValid =
    false;

  mirrorIsOn =
    false;


  onCandidate =
    false;

  offCandidate =
    false;


  logEvent(
    "MANUAL_POWER_ON"
  );
}


// ============================================================
// MANUAL POWER OFF
// ============================================================

void manualPowerOff()
{
  recovery.pending = false;
  recovery.attempts = 0;
  requireOffBeforeRearm = false;
  timerActive =
    false;

  resetInProgress =
    false;

  sensorLockout =
    false;


  mirrorStateValid =
    false;

  mirrorIsOn =
    false;


  onCandidate =
    false;

  offCandidate =
    false;


  setMirrorPower(
    false
  );


  logEvent(
    "MANUAL_POWER_OFF"
  );
}


// ============================================================
// AUTO TOGGLE
// ============================================================

void toggleAutomaticMode()
{
  autoEnabled =
    !autoEnabled;


  if (!autoEnabled)
  {
    if (timerActive)
    {
      stopTimer(
        "AUTO_DISABLED"
      );
    }

    logEvent(
      "AUTO_DISABLED"
    );
  }

  else
  {
    logEvent(
      "AUTO_ENABLED"
    );

    if (
      mirrorStateValid &&
      mirrorIsOn &&
      !requireOffBeforeRearm
    )
    {
      startTimer();
    }
  }
}



void processShutdownRecovery()
{
  if (!recovery.pending || resetInProgress || sensorLockout || otaInProgress) return;
  ShutdownRecovery::Action action = recovery.evaluate(millis(), mirrorStateValid, mirrorIsOn);
  if (action == ShutdownRecovery::Retry) {
    beginPowerCycle();
  } else if (action == ShutdownRecovery::LatchOff) {
    recovery.fault = true;
    recovery.pending = false;
    timerActive = false;
    requireOffBeforeRearm = false;
    mirrorStateValid = false;
    mirrorIsOn = false;
    setMirrorPower(false);
    if (fsOK) {
      File f = LittleFS.open(FAULT_FILE, "w");
      if (f) { f.print("1"); f.close(); }
    }
    logEvent("SHUTDOWN_FAILED_LATCHED_OFF");
  }
}

bool authenticateAdmin()
{
  if (server.authenticate("admin", ADMIN_PASSWORD)) return true;
  server.requestAuthentication(DIGEST_AUTH, "Mirror admin");
  return false;
}

bool validAdminRequest()
{
  return server.authenticate("admin", ADMIN_PASSWORD) &&
         server.hasArg("token") && server.arg("token") == adminNonce;
}

void handleUpdatePage()
{
  if (!authenticateAdmin()) return;
  String page = F("<!doctype html><html lang='sv'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Uppdatera spegeln</title><body style='font:18px system-ui;max-width:650px;margin:40px auto;padding:20px'>"
    "<h1>Uppdatera spegeln</h1><p>Välj firmwarefilen (.bin). Spegelns 12 V stängs av medan filen överförs. "
    "Låt styrenhetens ström vara ansluten tills den har startat om.</p><form method='POST' enctype='multipart/form-data' action='/update?token=");
  page += adminNonce;
  page += F("'><input type='file' name='firmware' accept='.bin' required><p><button>Installera uppdatering</button></p></form><a href='/'>Tillbaka</a></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleFirmwareUpload()
{
  HTTPUpload& upload = server.upload();
  otaLastActivity = millis();
  if (upload.status == UPLOAD_FILE_START) {
    otaAuthorized = validAdminRequest();
    otaSucceeded = false;
    otaFailed = false;
    if (!otaAuthorized) return;
    if (otaInProgress || upload.name != "firmware" || !upload.filename.endsWith(".bin")) {
      otaFailed = true;
      return;
    }
    // Abort automatic recovery as well: no power restoration during a flash write.
    manualPowerOff();
    logEvent("OTA_STARTED");
    otaInProgress = true;
    uint32_t freeSpace = ESP.getFreeSketchSpace();
    if (freeSpace <= 0x1000 || !Update.begin((freeSpace - 0x1000) & 0xFFFFF000, U_FLASH))
      otaFailed = true;
  } else if (otaAuthorized && upload.status == UPLOAD_FILE_WRITE && !otaFailed) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) otaFailed = true;
  } else if (otaAuthorized && upload.status == UPLOAD_FILE_END) {
    if (!otaFailed) otaSucceeded = Update.end(true);
    if (!otaSucceeded) {
      if (Update.isRunning()) Update.end();
      otaFailed = true;
    }
    otaInProgress = false;
    logEvent(otaSucceeded ? "OTA_SUCCESS" : "OTA_FAILED");
  } else if (otaAuthorized && upload.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.end();
    otaFailed = true;
    otaInProgress = false;
    logEvent("OTA_ABORTED");
  }
  yield();
}

void finishFirmwareUpload()
{
  if (!authenticateAdmin()) return;
  if (!validAdminRequest()) { server.send(403, "text/plain", "Invalid update token"); return; }
  if (otaInProgress) {
    if (Update.isRunning()) Update.end();
    otaInProgress = false;
    otaFailed = true;
    logEvent("OTA_FAILED");
  }
  if (!otaAuthorized || !otaSucceeded || otaFailed) {
    server.send(400, "text/plain; charset=utf-8", "Uppdateringen misslyckades. Välj rätt firmwarefil och försök igen. Om överföringen hade startat lämnas spegelmatningen av.");
    return;
  }
  server.send(200, "text/html; charset=utf-8", "<meta http-equiv='refresh' content='20;url=/'><h1>Uppdateringen är klar</h1><p>Spegelkontrollen startar om. Vänta cirka 20 sekunder.</p>");
  delay(300);
  ESP.restart();
}

void handleWiFiPage()
{
  if (!authenticateAdmin()) return;
  String page = F("<!doctype html><html lang='sv'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Spegelns Wi-Fi</title><body style='font:18px system-ui;max-width:650px;margin:40px auto;padding:20px'>"
    "<h1>Spegelns Wi-Fi</h1><p>Direktanslutning: Mirror-Setup. Öppna http://192.168.4.1 om mirror.local inte fungerar.</p>"
    "<form method='POST' action='/wifi'><label>Nätverksnamn<br><input name='ssid' maxlength='32' required></label><p>"
    "<label>Wi-Fi-lösenord<br><input type='password' name='password' maxlength='64'></label></p><input type='hidden' name='token' value='");
  page += adminNonce;
  page += F("'><button>Spara och anslut</button></form><p><a href='/'>Tillbaka</a></p></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", page);
}

void saveWiFi()
{
  if (!authenticateAdmin()) return;
  if (!validAdminRequest()) { server.send(403, "text/plain", "Invalid token"); return; }
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  if (!ssid.length() || ssid.length() > 32 || password.length() > 64) {
    server.send(400, "text/plain", "Invalid Wi-Fi settings"); return;
  }
  server.send(200, "text/html; charset=utf-8", "<h1>Inställningarna sparas</h1><p>Anslut till ditt nätverk och öppna http://mirror.local eller stanna på Mirror-Setup och öppna http://192.168.4.1.</p><a href='/'>Tillbaka</a>");
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), password.c_str());
  WiFi.persistent(false);
  logEvent("WIFI_SETTINGS_CHANGED");
}

// ============================================================
// WEB ROUTES
// ============================================================

void setupWebServer()
{
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, finishFirmwareUpload, handleFirmwareUpload);
  server.on("/wifi", HTTP_GET, handleWiFiPage);
  server.on("/wifi", HTTP_POST, saveWiFi);
  server.on("/log/old/download", HTTP_GET, []() {
    if (!LittleFS.exists(OLD_LOG_FILE)) { server.send(404, "text/plain", "No archived log"); return; }
    File f = LittleFS.open(OLD_LOG_FILE, "r");
    server.sendHeader("Content-Disposition", "attachment; filename=mirror_log_old.csv");
    server.streamFile(f, "text/csv");
    f.close();
  });
  server.on(
    "/",
    handleRoot
  );


  server.on(
    "/status",
    handleStatus
  );


  server.on(
    "/settime",
    []()
    {
      if (!server.hasArg("minutes"))
      {
        server.send(
          400,
          "text/plain",
          "Missing minutes"
        );

        return;
      }


      long minutes =
        server
          .arg("minutes")
          .toInt();


      if (
        minutes < 1 ||
        minutes > 1440
      )
      {
        server.send(
          400,
          "text/plain",
          "Minutes must be 1-1440"
        );

        return;
      }


      timerIntervalMs =
        (unsigned long)minutes *
        60000UL;


      saveSettings();


      if (
        timerActive &&
        mirrorIsOn
      )
      {
        timerStarted =
          millis();


        logEvent(
          "TIMER_CHANGED_RESTARTED_" +
          String(minutes) +
          "_MIN"
        );
      }
      else
      {
        logEvent(
          "TIMER_SET_" +
          String(minutes) +
          "_MIN"
        );
      }


      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );


  server.on(
    "/auto/toggle",
    []()
    {
      toggleAutomaticMode();


      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );


  server.on(
    "/power/on",
    []()
    {
      manualPowerOn();


      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );


  server.on(
    "/power/off",
    []()
    {
      manualPowerOff();


      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );


  server.on(
    "/reset",
    []()
    {
      if (
        powerOn &&
        !resetInProgress
      )
      {
        recovery.clear();
        timerActive =
          false;

        resetInProgress =
          true;

        requireOffBeforeRearm =
          false;

        resetStarted =
          millis();


        mirrorStateValid =
          false;

        mirrorIsOn =
          false;


        setMirrorPower(
          false
        );


        logEvent(
          "MANUAL_POWER_CYCLE"
        );
      }


      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );


  server.on(
    "/log",
    handleLogView
  );


  server.on(
    "/log/download",
    handleLogDownload
  );


  server.on(
    "/log/clear",
    []()
    {
      clearLog();


      server.send(
        200,
        "text/plain",
        "OK"
      );
    }
  );


  server.on("/wifi-reset", HTTP_GET, []() {
    server.sendHeader("Location", "/wifi");
    server.send(303);
  });

  server.onNotFound(
    []()
    {
      server.send(
        404,
        "text/plain",
        "Not found"
      );
    }
  );


  server.begin();


  Serial.println(
    "HTTP server started"
  );
}


// ============================================================
// WIFI
// ============================================================

void setupWiFi()
{
  // The controller runs even without a router; no blocking portal or restart loop.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname(MDNS_HOSTNAME);
  WiFi.softAP("Mirror-Setup", ADMIN_PASSWORD);
  WiFi.setAutoReconnect(true);
  WiFi.begin(); // Reuse credentials already stored by firmware 1.3.
  Serial.println("Direct Wi-Fi: Mirror-Setup / http://192.168.4.1");
}


// ============================================================
// mDNS
// ============================================================

void setupMDNS()
{
  if (mdnsStarted) MDNS.close();
  mdnsStarted = MDNS.begin(MDNS_HOSTNAME);
  if (mdnsStarted) MDNS.addService("http", "tcp", 80);
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(
    921600
  );


  delay(250);


  Serial.println();
  Serial.println(
    FW_VERSION
  );


  // ----------------------------------------------------------
  // MOSFET
  // ----------------------------------------------------------

  pinMode(
    MOSFET_PIN,
    OUTPUT
  );


  setMirrorPower(false);


  // ----------------------------------------------------------
  // LITTLEFS
  // ----------------------------------------------------------

  fsOK = LittleFS.begin();
  if (fsOK)
  {
    loadSession();

    loadSettings();


    if (!LittleFS.exists(LOG_FILE))
    {
      createLogHeader();
    }
  }
  else
  {
    Serial.println(
      "LittleFS ERROR"
    );
  }


  recovery.fault = fsOK && LittleFS.exists(FAULT_FILE);
  setMirrorPower(!recovery.fault);
  adminNonce = String(ESP.random(), HEX) + String(ESP.random(), HEX);

  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.begin(
    I2C_SDA_PIN,
    I2C_SCL_PIN
  );


  // ----------------------------------------------------------
  // INA219
  // ----------------------------------------------------------

  inaOK =
    ina219.begin();


  if (inaOK)
  {
    ina219.setCalibration_32V_2A();


    Serial.println(
      "INA219 OK"
    );
  }
  else
  {
    Serial.println(
      "INA219 ERROR"
    );
  }


  clearCurrentFilter();


  // ----------------------------------------------------------
  // PREFILL CURRENT FILTER
  // ----------------------------------------------------------

  if (inaOK)
  {
    for (
      uint8_t i = 0;
      i < FILTER_SIZE;
      i++
    )
    {
      float value =
        ina219.getCurrent_mA();


      if (value < 0)
        value = 0;


      addCurrentSample(
        value
      );


      delay(25);
    }
  }


  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  setupWiFi();


  // ----------------------------------------------------------
  // mDNS
  //
  // Start on the direct access point, then refresh when the station connects.
  // ----------------------------------------------------------

  setupMDNS();


  // ----------------------------------------------------------
  // WEB SERVER
  // ----------------------------------------------------------

  setupWebServer();


  // ----------------------------------------------------------
  // SENSOR STARTUP LOCKOUT
  // ----------------------------------------------------------

  sensorLockout =
    true;

  lockoutStarted =
    millis();


  mirrorStateValid =
    false;

  mirrorIsOn =
    false;


  requireOffBeforeRearm =
    false;


  // ----------------------------------------------------------
  // STARTUP LOG
  // ----------------------------------------------------------

  logEvent(
    "BOOT_SESSION_" +
    String(sessionNumber)
  );


  if (inaOK)
  {
    logEvent(
      "INA219_OK"
    );
  }
  else
  {
    logEvent(
      "INA219_ERROR"
    );
  }


  Serial.println();

  Serial.println(
    "--------------------------------"
  );

  Serial.println(
    "MR MATZO MIRROR READY"
  );

  Serial.println();

  Serial.print(
    "IP address:    http://"
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.println(
    "Friendly URL:  http://mirror.local"
  );

  Serial.println(
    "--------------------------------"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  // Web server
  server.handleClient();
  bool stationConnected = WiFi.status() == WL_CONNECTED;
  if (stationConnected != stationWasConnected) {
    stationWasConnected = stationConnected;
    setupMDNS();
  }


  // mDNS
  MDNS.update();


  if (otaInProgress) {
    if (millis() - otaLastActivity > 30000UL) {
      if (Update.isRunning()) Update.end();
      otaInProgress = false;
      otaFailed = true;
      logEvent("OTA_TIMEOUT");
    }
    yield();
    return;
  }

  // INA219
  readINA219();


  // Manual power cycle or bounded automatic shutdown attempt.
  processPowerCycle();


  // Sensor startup delay
  processSensorLockout();


  // Detect physical mirror light
  processMirrorDetection();


  // Never strand the controller in requireOffBeforeRearm.
  processShutdownRecovery();

  // Automatic shutdown timer
  processTimer();


  // Diagnostic log
  processLogging();


  // ----------------------------------------------------------
  // WIFI RECONNECT
  // ----------------------------------------------------------

  static unsigned long
    lastWiFiCheck = 0;


  if (
    millis() -
    lastWiFiCheck >=
    10000UL
  )
  {
    lastWiFiCheck =
      millis();


    if (
      WiFi.status() !=
      WL_CONNECTED
    )
    {
      WiFi.reconnect();
    }
  }


  yield();
}
