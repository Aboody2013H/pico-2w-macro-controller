# CircuitPython upstream source

The custom firmware was built from Adafruit CircuitPython commit
`bcfcb511352652d7cb62d3b415e4a624380f1830`.

The complete upstream checkout is intentionally not vendored into this repository:
it contains more than 26,000 files, hundreds of megabytes of third-party submodules,
and nested Git metadata. Reproduce it by cloning Adafruit CircuitPython at that
commit, then apply `patches/circuitpython-hid-1ms.patch` and build with the native
module under `usermod/bootsel/`.

All project-authored firmware source, the native module, the exact source snapshot,
build scripts, and compiled release UF2 files are included here.
