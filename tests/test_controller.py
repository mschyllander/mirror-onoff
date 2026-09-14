"""Compile and exercise the actual sketch's control functions on a host C++ compiler.

Run: python tests/test_controller.py
Hardware, clock and flash storage are stubbed. unsigned long is mapped to uint32_t
to reproduce ESP8266 millis() arithmetic on 64-bit desktop platforms.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[1]
sketch = root / 'firmware/esp_12V_mirror_timer/esp_12V_mirror_timer.ino'
source = sketch.read_text(encoding='utf-8')
names = ['clearCurrentFilter', 'addCurrentSample', 'startTimer', 'stopTimer',
         'getRemainingMs', 'beginPowerCycle', 'processTimer', 'processPowerCycle',
         'processSensorLockout', 'confirmedMirrorOff', 'confirmedMirrorOn',
         'processMirrorDetection', 'processShutdownRecovery', 'manualPowerOn',
         'manualPowerOff']
functions = []
for name in names:
    match = re.search(r'(?:void|unsigned long) ' + name + r'\([^)]*\)\s*\{', source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    functions.append(source[match.start():end])
globals_ = source[source.index('unsigned long timerIntervalMs'):source.index('// JSON ESCAPE')]
globals_ = globals_[:globals_.rfind('// ============================================================')]
prelude = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "ShutdownRecovery.h"
struct String : std::string {
  using std::string::string;
  String(const std::string& s):std::string(s){}
  String(unsigned int n):std::string(std::to_string(n)){}
};
std::string operator+(const char* a, const String& b) {return std::string(a)+std::string(b);}
std::string operator+(const String& a, const String& b) {return std::string(a)+std::string(b);}
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
bool faultFile = false;
struct File { operator bool() const {return true;} void print(const char*){faultFile=true;} void close(){} };
struct FS {File open(const char*, const char*) {return File();} void remove(const char*){faultFile=false;}} LittleFS;
const char* FAULT_FILE = "/shutdown_fault.txt";
bool fsOK = true, otaInProgress = false;
ShutdownRecovery recovery;
std::vector<std::string> events;
void logEvent(const String& e) { events.push_back(e); }
'''
tests = r'''
void resetFixture(uint32_t now=0) {
  clockMs=now; recovery.clear(); faultFile=false; events.clear();
  inaOK=true; powerOn=true; autoEnabled=true; timerActive=false;
  resetInProgress=false; sensorLockout=false; mirrorStateValid=true;
  mirrorIsOn=false; requireOffBeforeRearm=false; onCandidate=offCandidate=false;
  otaInProgress=false; fsOK=true; timerIntervalMs=900000; clearCurrentFilter();
}
void tick(uint32_t delta, float current) {
  clockMs+=delta; filteredCurrent_mA=current;
  processPowerCycle(); processSensorLockout(); processMirrorDetection();
  processShutdownRecovery(); processTimer();
}
void turnOn() {tick(0,524);tick(700,524);assert(timerActive);}
void expire() {tick(timerIntervalMs,524);assert(!powerOn && resetInProgress);}
void restoreAndDetect(float current, uint32_t offTime) {
  tick(offTime,current); assert(powerOn);
  tick(5000,current); tick(current<=26 ? 1000:700,current);
}
int main() {
  // Regression: yesterday's 524 mA after the first power cycle must not strand ON.
  resetFixture(); turnOn(); expire();
  restoreAndDetect(524,3000); assert(!powerOn && recovery.attempts==2);
  tick(9999,0); assert(!powerOn); tick(1,0); assert(powerOn);
  tick(5000,524); tick(700,524); assert(!powerOn && recovery.attempts==3);
  restoreAndDetect(524,30000);
  assert(recovery.fault && !powerOn && !timerActive && faultFile);
  tick(3600000,524); assert(!powerOn);
  manualPowerOn(); assert(powerOn && !recovery.fault && !faultFile);

  // Normal OFF after timeout clears recovery and permits the next full timer.
  resetFixture(); turnOn(); expire(); restoreAndDetect(21,3000);
  assert(!recovery.pending && !requireOffBeforeRearm && !timerActive && !mirrorIsOn);
  turnOn(); assert(getRemainingMs()==timerIntervalMs);

  // Second attempt can succeed without entering the third attempt.
  resetFixture(); turnOn(); expire(); restoreAndDetect(524,3000);
  restoreAndDetect(21,10000); assert(!recovery.pending && powerOn && !recovery.fault);

  // Hysteresis / transient rejection; manually extinguished light stops timer.
  resetFixture(); tick(0,31); tick(699,31); assert(!timerActive);
  tick(0,28); tick(1000,28); assert(!timerActive);
  tick(0,31); tick(700,31); assert(timerActive);
  tick(2000,28); assert(timerActive && mirrorIsOn);
  tick(0,26); tick(999,26); assert(timerActive);
  tick(1,26); assert(!timerActive && !mirrorIsOn);

  // Unknown current in the dead band cannot wait forever after restoration.
  resetFixture(); turnOn(); expire(); tick(3000,28); tick(5000,28);
  tick(9999,28); assert(recovery.attempts==1 && powerOn);
  tick(1,28); assert(recovery.attempts==2 && !powerOn);

  // A missing sensor during recovery still leads to bounded retries and OFF.
  resetFixture(); turnOn(); expire(); inaOK=false;
  for (int i=1;i<=3;++i) {tick(i==1?3000:i==2?10000:30000,0); tick(15000,0);}
  assert(recovery.fault && !powerOn);

  // Manual OFF cancels an in-progress restoration; OTA uses this same path.
  resetFixture(); turnOn(); expire(); manualPowerOff();
  tick(60000,0); assert(!powerOn && !recovery.pending && !resetInProgress);

  // millis() wrap during countdown and during recovery.
  resetFixture(UINT32_MAX-500); turnOn(); expire();
  restoreAndDetect(21,3000); assert(!recovery.pending);
  ShutdownRecovery wrap; wrap.beginAttempt(); wrap.restored(UINT32_MAX-1000);
  assert(wrap.evaluate(13998,false,false)==ShutdownRecovery::None);
  assert(wrap.evaluate(13999,false,false)==ShutdownRecovery::Retry);

  // Moving average, including a partially filled buffer and circular overwrite.
  resetFixture(); addCurrentSample(20); assert(filteredCurrent_mA==20);
  for(int i=0;i<9;++i) addCurrentSample(40);
  assert(filteredCurrent_mA==38); addCurrentSample(40); assert(filteredCurrent_mA==40);
  std::cout << "PASS: recovery, permanent OFF, rearm, thresholds, missing sensor, manual cancel, rollover, filter\n";
}
'''
declarations = '\n'.join(f[:f.index('{')].strip()+';' for f in functions)
body = prelude + globals_ + '\nvoid setMirrorPower(bool on){powerOn=on;}\n' + declarations + '\n' + '\n'.join(functions) + tests
body = body.replace('unsigned long', 'uint32_t')
with tempfile.TemporaryDirectory(prefix='mirror-tests-') as tmp:
    cpp = Path(tmp)/'test.cpp'
    exe = Path(tmp)/('test.exe' if os.name=='nt' else 'test')
    cpp.write_text(body,encoding='utf-8')
    subprocess.run([os.environ.get('CXX','g++'), '-std=c++11', '-Wall', '-Wextra',
                    '-I',str(sketch.parent),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
