/* Copy this file to `wifi_secret.h` (same directory) and fill in the
   real values. `wifi_secret.h` is gitignored so the credentials never
   land in version control.

   When present, main.cpp picks these up via __has_include and seeds
   them into NVS on the next CFG_VERSION migration so the board
   auto-connects without on-screen-keyboard input.

   Leaving the file absent is fine -- the firmware just starts with no
   default network and the user enters one through Settings -> Wi-Fi.
*/

#pragma once

#define DEFAULT_WIFI_SSID  "your-ssid-here"
#define DEFAULT_WIFI_PASS  "your-password-here"
