# RP2040 Pico BOOTSEL Autoclicker

Minimal native firmware for the original Raspberry Pi Pico (RP2040).

- Press BOOTSEL once to turn left-click turbo on.
- Press BOOTSEL again to turn it off.
- The onboard LED is lit while the autoclicker is active.
- Maximum-speed timing: 1 ms down, 1 ms up (about 500 clicks/second).
- Hold BOOTSEL while plugging in the Pico to enter normal UF2 flashing mode.

Builds are available for the original RP2040 Pico and the RP2350 Pico 2 W. The
Pico 2 W build initializes only the minimal CYW43 GPIO layer for its onboard LED;
Wi-Fi remains disabled. Both contain no web server, filesystem, Python runtime,
or client software.
