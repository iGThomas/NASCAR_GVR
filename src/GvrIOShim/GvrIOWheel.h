// GvrIOWheel - PC steering wheels (DirectInput, with force feedback) inside the GvrIO shim.
// See the header comment of GvrIOWheel.cpp.
#pragma once

// DllMain (via pad_attach): set up the lock. No I/O.
void wheel_attach(void);

// Pad discovery thread: find a wheel when there is none (reads [Wheel] once).
void wheel_discover(void);

// Game threads (inside read_pad): the wheel's steering -127..127, gas / brake 0..255 and its
// buttons as pad position bits. False when there is no wheel.
bool wheel_read(int* steer, int* gas, int* brake, unsigned* buttons);

// True for our own wheel device - GvrIOPad's shared GetDeviceState hook must leave it alone.
bool wheel_owns(void* device);

// Force feedback targets: force -10000..10000 (positive = to the right), springPct 0..100
// (-1 = the [Wheel] ForceSpring default). Applied by wheel_read at most every 10 ms.
void wheel_set_force(int force, int springPct);
