# Pico 2W Macro — native firmware

This is the native C++ port of the CircuitPython macro controller. It embeds
the existing UI, exposes the same HTTP API, uses TinyUSB keyboard/mouse HID,
persists custom macros in reserved flash, cycles through three Wi-Fi networks,
and serves the controller at `http://pico2w-macro.local/` when connected.

The tested Pico 2 W does not expose a reliable changing QSPI-CS button level
while Wi-Fi firmware is running, so runtime BOOTSEL polling is intentionally
disabled. Use the web controls for Mouse Turbo and the normal power-on BOOTSEL
procedure for UF2 updates.

Wi-Fi credentials are taken from the backed-up CircuitPython `settings.toml`
at build time and compiled into the UF2. The Pico does not need CircuitPython,
a client-side service, Node.js, or an SD card at runtime.
