#pragma once
#include <stdint.h>

// Bounded recovery: never leave an illuminated mirror waiting forever for OFF.
struct ShutdownRecovery {
  enum Action { None, Retry, LatchOff };
  uint8_t attempts = 0;
  bool pending = false;
  bool fault = false;
  uint32_t restoredAt = 0;

  void clear() { attempts = 0; pending = false; fault = false; }
  void beginAttempt() { ++attempts; pending = true; }
  uint32_t offTimeMs() const {
    return attempts <= 1 ? 3000U : attempts == 2 ? 10000U : 30000U;
  }
  void restored(uint32_t now) { restoredAt = now; }
  Action evaluate(uint32_t now, bool valid, bool lightOn) const {
    if (!pending || fault) return None;
    // OFF is cleared by the confirmed-OFF handler, after its confirmation time.
    if (valid && !lightOn) return None;
    if ((valid && lightOn) || uint32_t(now - restoredAt) >= 15000U)
      return attempts < 3 ? Retry : LatchOff;
    return None;
  }
};
