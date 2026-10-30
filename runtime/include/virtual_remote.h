#pragma once

#include "wii_remote_input.h"

#include <cstdint>

// Basara plays on a Classic Controller, so a keyboard, an ordinary gamepad or the touch overlay has
// to become one. Every port with no real Wii Remote but with an aurora PAD source (an SDL gamepad,
// or the keyboard when it is switched on for that port in the settings overlay) is presented to the
// game as a Wii Remote with a Classic Controller attached. aurora's PADRead supplies the state, which
// picks up the user's button remapping. Port 1 also always has a built-in keyboard layout, so the
// game is playable without any setup:
//
//   arrows        D-pad                       W A S D          left stick
//   X / Space     A                           I J K L          right stick
//   Z / L Shift   B                           Q / E            L / R
//   V             X                           1 / 3            ZL / ZR
//   C / L Ctrl    Y                           Enter / Esc      +
//   H             Home                        - / Tab          -
//
// Gamepads follow the Classic Controller by position, as in Dolphin's Classic layout: A east, B south,
// X north, Y west, triggers L and R, shoulders ZL and ZR, Start +, Back -, Guide Home, both sticks.

namespace VirtualRemote {

// True when `chan` should report a connected Wii Remote driven by a keyboard or gamepad.
bool Present(uint32_t chan);
// Fills one frame of the virtual remote on `chan`; false when Present(chan) is false.
bool Sample(uint32_t chan, WiiRemoteInput::KpadSample& sample);

// On-screen Classic Controller and phone motion for player 1 (the Android app's overlay, through
// JNI). tilt is -1 (counter-clockwise) .. 1 (clockwise). The bits match TouchControlsView.
enum TouchButton : uint32_t {
    kTouchLeft = 0x0001, kTouchRight = 0x0002, kTouchUp = 0x0004, kTouchDown = 0x0008,
    kTouchB = 0x0010, kTouchA = 0x0020, kTouchX = 0x0040, kTouchPlus = 0x0080, kTouchMinus = 0x0100,
    kTouchY = 0x0200, kTouchL = 0x0400, kTouchR = 0x0800, kTouchZL = 0x1000, kTouchZR = 0x2000,
    kTouchHome = 0x4000,
};
void SetTouchState(uint32_t touchButtons, bool shake, float tilt);
// The on-screen sticks, -1..1 with +y up.
void SetTouchSticks(float leftX, float leftY, float rightX, float rightY);

} // namespace VirtualRemote
