# Changelog

## 1.4.0 — 2026-09-14

- Fix the automatic shutdown deadlock when the mirror returns illuminated after a power cycle.
- Retry with 3, 10 and 30 second interruptions, then latch the supply off if shutdown cannot be confirmed.
- Preserve the shutdown fault across reboot in LittleFS; manual power-on acknowledges it.
- Add password-protected browser firmware updates at `/update` and a button in the existing Swedish UI.
- Add password-protected direct Wi-Fi access and `/wifi` configuration without blocking timer processing.
- Make the previous rotated log downloadable.
- Add regression tests for controller behavior and OTA upload handlers.

## 1.3

Original imported firmware: current-triggered timer, Swedish web interface, LittleFS logs and settings, and a single three-second power cycle. No OTA support.
