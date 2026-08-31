# Pico 2 W Macro Controller

A complete Raspberry Pi Pico 2 W macro system with a Pico-hosted web dashboard,
macro builder, three-network boot fallback, a native BOOTSEL API, approximately
500 CPS mouse turbo, LED feedback, and offline BOOTSEL operation.

Everything runs on the Pico itself. Node.js is only retained as the original UI
prototype and is not required by the final firmware.

## Releases

- **v1.0.0 — CircuitPython BOOTSEL Base:** CircuitPython 10.2.1 with the native
  `bootsel.pressed()` module.
- **v2.0.0 — BOOTSEL Autoclicker:** standalone native approximately-500-CPS
  autoclicker with onboard LED indication.
- **v3.0.0 — Complete Combined Build:** custom CircuitPython, web dashboard,
  macro builder, BOOTSEL autoclicker, 1 ms HID polling, and writable CIRCUITPY
  filesystem in one UF2.
- **v4.0.0 — Instant Response:** the complete combined build with BOOTSEL
  press-edge toggling and immediate website Mouse/Space activation. The former
  750 ms BOOTSEL hold and 250 ms web arming waits are removed.

Release files in this repository are credential-free. Configure your networks
in `settings.toml` after flashing; never commit that live file.

## Source layout

- `circuitpython_single_uf2/` — Python/web sources, filesystem and combined-UF2
  builders, native BOOTSEL module, firmware images, and source snapshot.
- `native_firmware/` — experimental Pico SDK C++ web/macro port.
- `pico_autoclicker/` — native RP2040 and RP2350 standalone autoclicker source.
- `app/`, `public/`, `worker/` — original polished web UI prototype.
- `release-assets/` — credential-free UF2 files attached to GitHub releases.
- `docs/` — complete project history, command audit, and live device metadata.

## BOOTSEL from CircuitPython

```python
import bootsel

if bootsel.pressed():
    print("BOOTSEL is held")
```

Use debounce in a real loop. BOOTSEL shares the flash chip-select line; the
custom native module pauses flash access and samples the line from RAM.

## Wi-Fi configuration

Copy `circuitpython_single_uf2/source_snapshot/settings.toml.example` to
`settings.toml` on CIRCUITPY and fill in up to three networks. They are attempted
in priority order during boot. If none connects, Wi-Fi stops until the next reset,
while BOOTSEL autoclicker operation remains available.

## Flashing

1. Unplug the Pico 2 W.
2. Hold BOOTSEL while reconnecting USB.
3. Release BOOTSEL when the `RP2350` drive appears.
4. Copy exactly one UF2 onto that drive.
5. Wait for the board to reboot; unplug/replug once if needed.

These Pico 2 W/RP2350A files are not for the original RP2040 Pico.

## Safety

A toggle autoclicker keeps clicking until BOOTSEL is pressed again or power is
removed. Keep the cursor away from controls. The combined build includes browser
heartbeat leases and emergency HID release logic, but physical caution still
matters.

See `docs/README.txt` for the full project changelog and `docs/COMMANDS.txt` for
the chronological computer-interaction audit.
