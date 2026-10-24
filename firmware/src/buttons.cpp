#include "buttons.h"

#include <Arduino.h>

#include "board.h"

namespace {

const uint32_t DEBOUNCE_MS = 30;
const uint32_t MODE_LONG_MS = 3000;   // hotspot on/off
const uint32_t BOOT_LONG_MS = 10000;  // factory reset

struct Button {
  uint8_t pin;
  uint32_t longMs;
  bool down = false;
  bool longFired = false;
  uint32_t changedMs = 0, pressedMs = 0;
  bool raw = false;

  // 0 nothing, 1 short press (on release), 2 long press (while held)
  int poll(uint32_t now) {
    bool r = digitalRead(pin) == LOW;
    if (r != raw) {
      raw = r;
      changedMs = now;
    }
    if (now - changedMs < DEBOUNCE_MS || raw == down) {
      if (down && !longFired && now - pressedMs >= longMs) {
        longFired = true;
        return 2;
      }
      return 0;
    }
    down = raw;
    if (down) {
      pressedMs = now;
      longFired = false;
      return 0;
    }
    return longFired ? 0 : 1;
  }
};

Button mode{board::PIN_MODE_BTN, MODE_LONG_MS};
Button boot{board::PIN_BOOT_BTN, BOOT_LONG_MS};

}  // namespace

void buttons::begin() {
  // MODE has no external pull-up on the board (C1012 is DNP).
  pinMode(board::PIN_MODE_BTN, INPUT_PULLUP);
  pinMode(board::PIN_BOOT_BTN, INPUT_PULLUP);
}

ButtonEvent buttons::poll() {
  uint32_t now = millis();
  int m = mode.poll(now);
  if (m == 1) return ButtonEvent::ModeShort;
  if (m == 2) return ButtonEvent::ModeLong;
  // IO0 short press does nothing; only the 10 s hold counts.
  if (boot.poll(now) == 2) return ButtonEvent::BootLong;
  return ButtonEvent::None;
}
