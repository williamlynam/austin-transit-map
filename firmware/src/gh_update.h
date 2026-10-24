// Firmware updates from the newest release on GitHub. The web page asks for
// a check or an install; the network task does the work between feed polls,
// so the update download never needs memory at the same time as the feed.
#pragma once
#include <Arduino.h>

#include "settings.h"

namespace ghupdate {

enum class State : uint8_t { Idle, Checking, UpToDate, Available, Installing, Failed };

struct Info {
  State state = State::Idle;
  String latest;      // newest release's version, "0.1.16"
  bool prerelease = false;
  String error;
  uint32_t checkedAtMs = 0;  // millis() of the last finished check, 0 = never
  uint32_t done = 0, total = 0;  // install progress, bytes
};

void requestCheck();
void requestInstall();
Info info();
const char *stateName(State s);

// Called by the network task on every pass. Does nothing unless a check or
// install was asked for, or the daily check is due.
void service(const Settings &s, bool online);

}  // namespace ghupdate
