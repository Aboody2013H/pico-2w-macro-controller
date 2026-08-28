# Pico 2 W complete CircuitPython recovery UF2

This packages the official CircuitPython 10.2.1 Pico 2 W firmware together with
an exact image of the working CIRCUITPY drive. One BOOTSEL flash restores the
Python program, both web pages, libraries, settings, and writable macro storage.

The filesystem remains a normal writable CIRCUITPY drive after flashing. Changes
made later are stored on the Pico but do not rewrite the recovery UF2.

Flash target: Raspberry Pi Pico 2 W (`rp2350-arm-s`) only.

## WiFi + BOOTSEL fallback build

`Pico-2W-Macro-Web-BOOTSEL-Fallback-Complete.uf2` combines the complete website
setup with a custom CircuitPython 10.2.1 firmware module that safely reads the
Pico 2 W BOOTSEL line from RAM.

- When WiFi connects, the normal website and macro builder run.
- BOOTSEL toggles the local left-clicker whether WiFi is connected or not.
- The website Mouse Turbo control and BOOTSEL share the same 1 ms HID engine.
- The onboard LED shows the clicker's state.
- WiFi retries occur only while the offline clicker is stopped.

The final build is `Pico-2W-Macro-Web-BOOTSEL-Always-500CPS-Complete.uf2`.

The boot-scan-only variant is
`Pico-2W-Macro-Web-BOOTSEL-500CPS-BootScanOnly.uf2`. It performs one saved
network scan/connection pass during startup. If none connects, WiFi station
mode is stopped and no further scan occurs until Ctrl+D/reset or a power cycle.
Local controls and macro ticks are serviced between scan results, and a later
WiFi loss does not stop active macros.
