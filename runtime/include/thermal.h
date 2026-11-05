#pragma once

// Android 12+: the system's thermal headroom (AThermal_getThermalHeadroom), polled on a background
// thread every two seconds. 1.0 is the "severe" level where the device throttles hard; the value is
// a forecast ten seconds ahead, so work can be shed before the clocks fall. Elsewhere, and when the
// API is missing, Headroom() is negative (unknown).
namespace Thermal {

void Start();          // idempotent; BASARA_THERMAL=0 keeps it off
float Headroom();      // < 0: unknown
int Status();          // AThermal status 0 (none) .. 6 (shutdown), -1 unknown

// Hysteresis on the headroom: hot from kHotHeadroom, cool again below kCoolHeadroom.
constexpr float kHotHeadroom = 0.85f;
constexpr float kCoolHeadroom = 0.78f;
// Hot and the player's Thermal Control setting is on ([video] thermal_control, default on).
bool Hot();
// Earlier level, for shedding drawing work before the phone gets hot in the hand: warm from
// kWarmHeadroom, cool again below kWarmCoolHeadroom. Also gated on Thermal Control.
// G81 already reads ~0.73 in the menu, so 0.75 fired at once. The release sits close to the entry:
// a phone that warmed up once hovers at 0.72-0.80 for long, which kept it at 30 fps, and the
// one-minute hold in the frame loop (kThermalHoldSeconds) already keeps it from flapping.
constexpr float kWarmHeadroom = 0.82f;
constexpr float kWarmCoolHeadroom = 0.80f;
bool Warm();
void SetEnabled(bool enabled);

} // namespace Thermal
