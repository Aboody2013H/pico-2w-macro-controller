PICO 2W MACRO CONTROLLER + CUSTOM MACRO BUILDER

RUNTIME
- The Pico 2W is the only server and the source of truth.
- index.html is the main controller page.
- builder.html is the custom macro builder page.
- Custom macros are stored in the Pico 2W's separate non-volatile memory.
- CIRCUITPY remains normally writable from your computer.
- No boot.py, filesystem remount, Node.js, cloud service, CDN, localStorage,
  or second server is used.

INSTALL
1. Install CircuitPython on the Pico 2W.
2. Copy code.py, index.html, and builder.html to CIRCUITPY.
3. Install adafruit_httpserver and adafruit_hid in CIRCUITPY/lib if they are
   not already present.
4. Rename settings.toml.example to settings.toml and enter Wi-Fi details.
5. Eject CIRCUITPY and fully power-cycle the Pico.
6. Open the IP address printed in the CircuitPython serial console.

UPGRADING FROM THE PREVIOUS READ-ONLY BUILD
1. Connect GP15 to GND and power on one final time so CIRCUITPY is writable.
2. Delete the old boot.py and macros.json from CIRCUITPY.
3. Copy this bundle's code.py, index.html, and builder.html.
4. Remove the GP15-to-GND wire and power-cycle normally.
After that, no maintenance wire or read-only mode is used again.

REQUIRED CIRCUITPY LIBRARIES
- adafruit_httpserver
- adafruit_hid

CUSTOM MACRO STORAGE
- Macro data is compactly encoded in microcontroller.nvm with a length header
  and checksum.
- Saving, editing, and deleting macros does not write to the CIRCUITPY drive.
- The builder reports a clear error if the board's NVM capacity is reached.

CUSTOM MACRO SAFETY
- The engine is cooperative and never loops inside an HTTP request.
- Each main-loop pass handles HTTP and advances only due macro actions.
- Press-and-hold macros use a browser heartbeat and stop automatically if the
  controlling page disconnects.
- STOP ALL releases every keyboard key and mouse button and clears all states.
- A repeating task releases anything it still holds after each full sequence.
- The UI permits up to 12 macros with up to 32 actions each, subject to the
  Pico's available NVM capacity.

SWEDISH KEYBOARD
Å, Ä, Ö, AltGr, and Swedish symbol keys use their physical Swedish QWERTY HID
positions. Set the connected computer's operating-system keyboard layout to
Swedish for the labels to match the output.
