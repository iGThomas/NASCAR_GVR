// GvrIOPad - Xbox / PlayStation controller support inside the GvrIO shim.
// See the header comment of GvrIOPad.cpp for how a pad reaches NASCAR.
#pragma once
#include <windows.h>

// DllMain: remember our module and which program hosts us. Does no I/O (loader lock).
void pad_attach(HMODULE self, bool isShell);

// cGvrIO::Initialize override, BEFORE the OEM Initialize runs: hook GvrIO_oem.dll's
// DirectInput8Create so the pad's buttons can be added to GvrIO's own keyboard state.
void pad_hook_gvrio(HMODULE oem);

// GvrIOMessageSend msg 5 (live axis, device 1) when no real wheel/pedals are fitted:
// fills *value (0 steering -127..127, 1 gas / 2 brake 0..255) and returns true when a
// pad is connected; returns false when there is no pad, so the caller's own fallback runs.
bool pad_axis(int axis, int* value);

// True while a window of this process is the foreground window.
bool pad_process_has_focus(void);

// Full path of the install's nascar_settings.ini (searched upwards from this DLL); false if
// there is none. Valid once pad_attach has run.
bool shim_settings_path(char* out, size_t n);

// Shared with GvrIOShim.cpp: the GVRIOSHIM_LOG trace (gvrioshim.log next to the exe).
void shim_log(const char* fmt, ...);

// GvrIOReg.cpp: answer HKLM\SOFTWARE\GlobalVR and \gvr from <install>\nascar_registry.ini.
// Call from DllMain after pad_attach (it needs the install folder).
void reg_attach(void);
