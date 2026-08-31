"""Hide the CircuitPython USB mass-storage drive on normal boots."""

import storage

storage.disable_usb_drive()
