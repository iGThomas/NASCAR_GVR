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

// True while a wheel or a pedal box is open: its axes then win over the OEM's (GvrIOShim msg 5).
bool wheel_present(void);

// Developer test (GVRIOSHIM_TEST_PAD_AS_WHEEL): the pad code's own trigger readings (0..255),
// so the stand-in works over Bluetooth too, where DirectInput gets no updates from a PS4 pad.
void wheel_test_feed(bool present, int steer, int gas, int brake);

// True while a steering wheel is open (false with only a pedal box: steering stays on the pad).
bool wheel_steers(void);

// The cabinet I/O board's pedal calibration, played for PC pedals (axis 1 gas, 2 brake):
// raw position / min / max 0..255; false = no PC pedals with own axes (the shim answers as before).
bool wheel_cal_axis(int axis, int* raw, int* mn, int* mx);
// Reset it (msg 0x53: the shell's calibration screen, operator Control Test Start + Music).
bool wheel_cal_reset(void);
// One discovery pass now if none ran yet (the shell's start-up pedal check comes early).
void wheel_discover_first(void);
