import json
import os
import time

import board
import digitalio
import microcontroller
import socketpool
import usb_hid
import wifi

from adafruit_httpserver import GET, POST, FileResponse, JSONResponse, Response, Server
from adafruit_hid.keyboard import Keyboard
from adafruit_hid.keyboard_layout_us import KeyboardLayoutUS
from adafruit_hid.keycode import Keycode
from adafruit_hid.mouse import Mouse

try:
    import bootsel
except ImportError:
    # The website still works on ordinary CircuitPython. BOOTSEL fallback is
    # enabled by the custom all-in-one firmware.
    bootsel = None


MAX_MACROS = 12
MAX_STEPS = 32
MAX_DELAY_MS = 60000
MAX_REPEAT_MS = 600000
HOLD_LEASE_SECONDS = 2.5
BUILTIN_HOLD_LEASE_SECONDS = 1.5
WIFI_CONNECT_TIMEOUT = 10
WIFI_ATTEMPTS_PER_NETWORK = 2
FAST_CLICK_BURST = 8
SPACE_TAP_INTERVAL_SECONDS = 1 / 60


try:
    wifi.radio.power_management = wifi.PowerManagement.NONE
except Exception as error:
    print("WiFi power management:", error)

led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT
led.value = False

mouse = Mouse(usb_hid.devices)
keyboard = Keyboard(usb_hid.devices)
layout = KeyboardLayoutUS(keyboard)


KEYCODES = {}
for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    KEYCODES[letter] = getattr(Keycode, letter)

KEYCODES.update({
    "0": Keycode.ZERO, "1": Keycode.ONE, "2": Keycode.TWO,
    "3": Keycode.THREE, "4": Keycode.FOUR, "5": Keycode.FIVE,
    "6": Keycode.SIX, "7": Keycode.SEVEN, "8": Keycode.EIGHT,
    "9": Keycode.NINE,
    "AA": Keycode.LEFT_BRACKET,       # Å on a Swedish keyboard
    "AE": Keycode.QUOTE,              # Ä on a Swedish keyboard
    "OE": Keycode.SEMICOLON,          # Ö on a Swedish keyboard
    "SECTION": Keycode.GRAVE_ACCENT,  # § / ½
    "PLUS": Keycode.MINUS,            # + / ?
    "ACUTE": Keycode.EQUALS,          # ´ / `
    "DIAERESIS": Keycode.RIGHT_BRACKET,
    "APOSTROPHE": Keycode.BACKSLASH,
    "LESS_THAN": Keycode.KEYPAD_BACKSLASH,
    "COMMA": Keycode.COMMA,
    "PERIOD": Keycode.PERIOD,
    "HYPHEN": Keycode.FORWARD_SLASH,
    "CTRL": Keycode.LEFT_CONTROL,
    "SHIFT": Keycode.LEFT_SHIFT,
    "ALT": Keycode.LEFT_ALT,
    "ALTGR": Keycode.RIGHT_ALT,
    "GUI": Keycode.WINDOWS,
    "RCTRL": Keycode.RIGHT_CONTROL,
    "RSHIFT": Keycode.RIGHT_SHIFT,
    "SPACE": Keycode.SPACE,
    "ENTER": Keycode.ENTER,
    "TAB": Keycode.TAB,
    "ESC": Keycode.ESCAPE,
    "BACKSPACE": Keycode.BACKSPACE,
    "DELETE": Keycode.DELETE,
    "INSERT": Keycode.INSERT,
    "HOME": Keycode.HOME,
    "END": Keycode.END,
    "PAGE_UP": Keycode.PAGE_UP,
    "PAGE_DOWN": Keycode.PAGE_DOWN,
    "UP": Keycode.UP_ARROW,
    "DOWN": Keycode.DOWN_ARROW,
    "LEFT": Keycode.LEFT_ARROW,
    "RIGHT": Keycode.RIGHT_ARROW,
    "CAPS_LOCK": Keycode.CAPS_LOCK,
    "NUM_LOCK": Keycode.KEYPAD_NUMLOCK,
    "SCROLL_LOCK": Keycode.SCROLL_LOCK,
    "PRINT_SCREEN": Keycode.PRINT_SCREEN,
    "PAUSE": Keycode.PAUSE,
    "MENU": Keycode.APPLICATION,
    "KP_0": Keycode.KEYPAD_ZERO,
    "KP_1": Keycode.KEYPAD_ONE,
    "KP_2": Keycode.KEYPAD_TWO,
    "KP_3": Keycode.KEYPAD_THREE,
    "KP_4": Keycode.KEYPAD_FOUR,
    "KP_5": Keycode.KEYPAD_FIVE,
    "KP_6": Keycode.KEYPAD_SIX,
    "KP_7": Keycode.KEYPAD_SEVEN,
    "KP_8": Keycode.KEYPAD_EIGHT,
    "KP_9": Keycode.KEYPAD_NINE,
    "KP_PLUS": Keycode.KEYPAD_PLUS,
    "KP_MINUS": Keycode.KEYPAD_MINUS,
    "KP_MULTIPLY": Keycode.KEYPAD_ASTERISK,
    "KP_DIVIDE": Keycode.KEYPAD_FORWARD_SLASH,
    "KP_PERIOD": Keycode.KEYPAD_PERIOD,
    "KP_ENTER": Keycode.KEYPAD_ENTER,
})

for number in range(1, 13):
    KEYCODES["F" + str(number)] = getattr(Keycode, "F" + str(number))

MOUSE_BUTTONS = {
    "MOUSE_LEFT": Mouse.LEFT_BUTTON,
    "MOUSE_RIGHT": Mouse.RIGHT_BUTTON,
    "MOUSE_MIDDLE": Mouse.MIDDLE_BUTTON,
    "MOUSE_BACK": Mouse.BACK_BUTTON,
    "MOUSE_FORWARD": Mouse.FORWARD_BUTTON,
}

ACTION_PACK = {"tap": "t", "click": "c", "down": "d", "up": "u"}
ACTION_UNPACK = {"t": "tap", "c": "click", "d": "down", "u": "up"}
MODE_PACK = {"normal": "n", "hold": "h", "toggle": "t"}
MODE_UNPACK = {"n": "normal", "h": "hold", "t": "toggle"}


def load_macros():
    memory = microcontroller.nvm
    try:
        if len(memory) < 6 or memory[0] != 80 or memory[1] != 77:
            return []
        length = (memory[2] << 8) | memory[3]
        expected_checksum = (memory[4] << 8) | memory[5]
        if length <= 0 or length + 6 > len(memory):
            return []
        payload = bytes(memory[6:6 + length])
        if (sum(payload) & 0xFFFF) != expected_checksum:
            return []
        packed = json.loads(payload.decode("utf-8"))
        result = []
        for item in packed:
            steps = []
            for step in item[4]:
                steps.append({
                    "device": "mouse" if step[0] in MOUSE_BUTTONS else "key",
                    "code": step[0], "label": step[0],
                    "action": ACTION_UNPACK[step[1]], "delay": step[2],
                })
            result.append({
                "id": item[0], "name": item[1], "mode": MODE_UNPACK[item[2]],
                "repeat": item[3], "steps": steps,
            })
        return result
    except (ValueError, TypeError, IndexError, KeyError, UnicodeError):
        return []


macros = load_macros()
tasks = []
key_refs = {}
mouse_refs = {}
mouse_active = False
space_active = False
walk_active = False
bootsel_click_active = False
fast_mouse_down = False
button_raw = False
next_space_tap = 0.0
mouse_lease_until = 0.0
space_lease_until = 0.0
mouse_arm_started = None
space_arm_started = None


def stop_bootsel_clicker():
    global bootsel_click_active, fast_mouse_down
    bootsel_click_active = False
    if fast_mouse_down:
        mouse.release(Mouse.LEFT_BUTTON)
        fast_mouse_down = False


def emergency_release_all():
    """Fail closed: clear every mode and release all USB HID state."""
    global mouse_active, space_active, walk_active
    global mouse_lease_until, space_lease_until
    global mouse_arm_started, space_arm_started
    mouse_active = False
    space_active = False
    walk_active = False
    mouse_lease_until = 0.0
    space_lease_until = 0.0
    mouse_arm_started = None
    space_arm_started = None
    stop_bootsel_clicker()
    stop_all_custom()
    key_refs.clear()
    mouse_refs.clear()
    keyboard.release_all()
    mouse.release_all()
    led.value = False


def poll_bootsel_button(now):
    """Toggle immediately on the BOOTSEL press edge, online or offline."""
    global bootsel_click_active, fast_mouse_down
    global button_raw

    if bootsel is None:
        return

    sample = bootsel.pressed()
    if sample and not button_raw:
        if bootsel_click_active:
            stop_bootsel_clicker()
        else:
            emergency_release_all()
            bootsel_click_active = True
        print("BOOTSEL autoclicker:", "ON" if bootsel_click_active else "OFF")
        if (not bootsel_click_active and not mouse_active and fast_mouse_down):
            mouse.release(Mouse.LEFT_BUTTON)
            fast_mouse_down = False
    button_raw = sample


def run_fast_mouse_clicker(now):
    """Run complete click pairs at the 1 ms HID endpoint's full rate."""
    global fast_mouse_down
    active = bootsel_click_active or mouse_active
    if not active:
        if fast_mouse_down:
            mouse.release(Mouse.LEFT_BUTTON)
            fast_mouse_down = False
        return

    # usb_hid.send_report() already waits for the next 1 ms endpoint slot.
    # A click is exactly two reports (down + up), so no additional Python timer
    # belongs here. Short bursts amortize web-loop overhead while preserving an
    # approximately 16 ms worst-case stop/poll response time.
    fast_mouse_down = False
    for _ in range(FAST_CLICK_BURST):
        if not (bootsel_click_active or mouse_active):
            break
        mouse.click(Mouse.LEFT_BUTTON)
        poll_bootsel_button(time.monotonic())


def service_builtin_holds(now):
    """Stop a web hold automatically if its browser heartbeat disappears."""
    global mouse_active, space_active, next_space_tap
    global mouse_arm_started, space_arm_started
    if mouse_arm_started is not None and now > mouse_lease_until:
        mouse_active = False
        mouse_arm_started = None
    if space_arm_started is not None and now > space_lease_until:
        space_active = False
        space_arm_started = None
    if space_active and now >= next_space_tap:
        key_tap("SPACE")
        next_space_tap = now + SPACE_TAP_INTERVAL_SECONDS


def save_macros():
    packed = []
    for macro in macros:
        packed_steps = []
        for step in macro["steps"]:
            packed_steps.append([
                step["code"], ACTION_PACK[step["action"]], step["delay"]
            ])
        packed.append([
            macro["id"], macro["name"], MODE_PACK[macro["mode"]],
            macro["repeat"], packed_steps
        ])
    payload = json.dumps(packed).encode("utf-8")
    memory = microcontroller.nvm
    if len(payload) + 6 > len(memory):
        raise ValueError("Macro storage full ({} bytes available)".format(len(memory) - 6))
    checksum = sum(payload) & 0xFFFF
    memory[0:2] = b"\x00\x00"
    memory[6:6 + len(payload)] = payload
    memory[2:6] = bytes((
        (len(payload) >> 8) & 255, len(payload) & 255,
        (checksum >> 8) & 255, checksum & 255,
    ))
    memory[0:2] = b"PM"


def find_macro(macro_id):
    for macro in macros:
        if macro.get("id") == macro_id:
            return macro
    return None


def next_macro_id():
    highest = 0
    for macro in macros:
        try:
            highest = max(highest, int(macro.get("id", "m0")[1:]))
        except (ValueError, TypeError):
            pass
    return "m" + str(highest + 1)


def clean_integer(value, minimum, maximum):
    value = int(value)
    if value < minimum or value > maximum:
        raise ValueError("Number outside allowed range")
    return value


def validate_macro(data):
    if not isinstance(data, dict):
        raise ValueError("Invalid macro")
    name = str(data.get("name", "")).strip()
    if not name or len(name) > 32:
        raise ValueError("Name must be 1–32 characters")
    mode = data.get("mode")
    if mode not in ("normal", "hold", "toggle"):
        raise ValueError("Invalid execution mode")
    repeat = clean_integer(data.get("repeat", 0), 0, MAX_REPEAT_MS)
    source_steps = data.get("steps", [])
    if not isinstance(source_steps, list) or not source_steps or len(source_steps) > MAX_STEPS:
        raise ValueError("A macro needs 1–32 steps")

    steps = []
    for source in source_steps:
        if not isinstance(source, dict):
            raise ValueError("Invalid step")
        code = source.get("code")
        action = source.get("action")
        if code in KEYCODES:
            device = "key"
            allowed = ("tap", "down", "up")
        elif code in MOUSE_BUTTONS:
            device = "mouse"
            allowed = ("click", "down", "up")
        else:
            raise ValueError("Unsupported key")
        if action not in allowed:
            raise ValueError("Unsupported action")
        steps.append({
            "device": device,
            "code": code,
            "label": str(source.get("label", code))[:20],
            "action": action,
            "delay": clean_integer(source.get("delay", 0), 0, MAX_DELAY_MS),
        })

    return {
        "id": str(data.get("id", "")),
        "name": name,
        "mode": mode,
        "repeat": repeat,
        "steps": steps,
    }


def key_down(task, code):
    if code in task["held_keys"]:
        return
    keycode = KEYCODES[code]
    count = key_refs.get(keycode, 0)
    if count == 0:
        keyboard.press(keycode)
    key_refs[keycode] = count + 1
    task["held_keys"].append(code)


def key_up(task, code):
    if code not in task["held_keys"]:
        return
    keycode = KEYCODES[code]
    task["held_keys"].remove(code)
    count = key_refs.get(keycode, 0) - 1
    if count <= 0:
        key_refs.pop(keycode, None)
        keyboard.release(keycode)
    else:
        key_refs[keycode] = count


def key_tap(code):
    keycode = KEYCODES[code]
    if key_refs.get(keycode, 0) == 0:
        keyboard.press(keycode)
        keyboard.release(keycode)


def mouse_down(task, code):
    if code in task["held_mouse"]:
        return
    button = MOUSE_BUTTONS[code]
    count = mouse_refs.get(button, 0)
    if count == 0:
        mouse.press(button)
    mouse_refs[button] = count + 1
    task["held_mouse"].append(code)


def mouse_up(task, code):
    if code not in task["held_mouse"]:
        return
    button = MOUSE_BUTTONS[code]
    task["held_mouse"].remove(code)
    count = mouse_refs.get(button, 0) - 1
    if count <= 0:
        mouse_refs.pop(button, None)
        mouse.release(button)
    else:
        mouse_refs[button] = count


def mouse_click(code):
    button = MOUSE_BUTTONS[code]
    if mouse_refs.get(button, 0) == 0:
        mouse.click(button)


def release_task_holds(task):
    for code in task["held_keys"][:]:
        key_up(task, code)
    for code in task["held_mouse"][:]:
        mouse_up(task, code)


def stop_task(macro_id):
    for task in tasks[:]:
        if task["id"] == macro_id:
            release_task_holds(task)
            tasks.remove(task)


def stop_all_custom():
    for task in tasks[:]:
        release_task_holds(task)
    tasks.clear()


def start_task(macro, repeating, hold_lease=False):
    stop_task(macro["id"])
    now = time.monotonic()
    tasks.append({
        "id": macro["id"],
        "macro": macro,
        "repeating": repeating,
        "hold_lease": hold_lease,
        "lease_until": now + HOLD_LEASE_SECONDS if hold_lease else 0,
        "index": 0,
        "next_time": now,
        "held_keys": [],
        "held_mouse": [],
    })


def run_macro_engine(now):
    for task in tasks[:]:
        if task not in tasks:
            continue
        if task["hold_lease"] and now > task["lease_until"]:
            stop_task(task["id"])
            continue
        if now < task["next_time"]:
            continue

        steps = task["macro"]["steps"]
        if task["index"] >= len(steps):
            release_task_holds(task)
            if task["repeating"]:
                task["index"] = 0
                task["next_time"] = now + task["macro"]["repeat"] / 1000
            else:
                stop_task(task["id"])
            continue

        step = steps[task["index"]]
        try:
            if step["device"] == "key":
                if step["action"] == "down":
                    key_down(task, step["code"])
                elif step["action"] == "up":
                    key_up(task, step["code"])
                else:
                    key_tap(step["code"])
            else:
                if step["action"] == "down":
                    mouse_down(task, step["code"])
                elif step["action"] == "up":
                    mouse_up(task, step["code"])
                else:
                    mouse_click(step["code"])
        except Exception as error:
            print("Macro step error:", error)
            stop_task(task["id"])
            continue

        task["index"] += 1
        task["next_time"] = now + step["delay"] / 1000


def active_ids():
    return [task["id"] for task in tasks]


networks = [
    (os.getenv("WIFI1_SSID"), os.getenv("WIFI1_PASSWORD")),
    (os.getenv("WIFI2_SSID"), os.getenv("WIFI2_PASSWORD")),
    (os.getenv("WIFI3_SSID"), os.getenv("WIFI3_PASSWORD")),
]


def service_macros_during_wifi():
    """Keep local controls and running macros responsive during a scan."""
    now = time.monotonic()
    poll_bootsel_button(now)
    run_macro_engine(now)
    service_builtin_holds(now)
    run_fast_mouse_clicker(time.monotonic())
    led.value = bool(bootsel_click_active or mouse_active or
                     space_active or walk_active or tasks)


def wifi_settle(seconds=0.6):
    """Let the CYW43 driver settle while local controls remain responsive."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        service_macros_during_wifi()
        time.sleep(0.01)


def scan_wifi_names():
    """Return visible SSIDs without letting a failed scan stop recovery."""
    available = []
    scanning = False
    try:
        scanning = True
        for network in wifi.radio.start_scanning_networks():
            service_macros_during_wifi()
            if network.ssid not in available:
                available.append(network.ssid)
    except Exception as error:
        print("WiFi scan failed:", error)
        return None
    finally:
        if scanning:
            try:
                wifi.radio.stop_scanning_networks()
            except Exception:
                pass
    return available


def connect_saved_network():
    """Run the single boot-time WIFI1/WIFI2/WIFI3 connection pass."""
    try:
        wifi.radio.start_station()
    except Exception:
        pass
    configured = [(ssid, password) for ssid, password in networks
                  if ssid and password]
    if not configured:
        print("No WiFi networks configured in settings.toml")
        return False

    print("Scanning WiFi networks...")
    available = scan_wifi_names()
    if available is not None:
        visible_saved = [ssid for ssid, _password in configured
                         if ssid in available]
        if visible_saved:
            print("Visible saved WiFi:", ", ".join(visible_saved))
        else:
            print("No saved WiFi appeared in scan; trying all anyway")

    # A single scan can miss a network, especially a phone hotspot. Always try
    # every configured entry in priority order so WIFI2/WIFI3 are true fallbacks.
    for ssid, password in configured:
        for attempt in range(1, WIFI_ATTEMPTS_PER_NETWORK + 1):
            try:
                service_macros_during_wifi()
                print("Trying WiFi: {} ({}/{})".format(
                    ssid, attempt, WIFI_ATTEMPTS_PER_NETWORK))
                # connect() performs its own station reset on RP2350.
                wifi.radio.connect(ssid, password,
                                   timeout=WIFI_CONNECT_TIMEOUT)
                service_macros_during_wifi()
                if (wifi.radio.connected and
                        wifi.radio.ipv4_address is not None):
                    print("Connected to:", ssid)
                    return True
            except Exception as error:
                print("WiFi failed:", ssid, error)
            service_macros_during_wifi()
            wifi_settle()
    return False


pool = socketpool.SocketPool(wifi.radio)
server = Server(pool)
# The default is only 1024 bytes. A 32-step macro JSON payload needs more.
server.request_buffer_size = 12288


@server.route("/", GET)
def home(request):
    return FileResponse(request, "index.html", "/", headers={
        "Cache-Control": "no-store, max-age=0",
        "Pragma": "no-cache",
    })


@server.route("/builder", GET)
def builder(request):
    return FileResponse(request, "builder.html", "/", headers={
        "Cache-Control": "no-store, max-age=0",
        "Pragma": "no-cache",
    })


@server.route("/api/macros", GET)
def get_macros(request):
    return JSONResponse(request, {"macros": macros})


@server.route("/api/status", GET)
def get_status(request):
    return JSONResponse(request, {
        "active": active_ids(),
        "mouse": mouse_active or bootsel_click_active,
        "space": space_active,
        "walk": walk_active,
    })


@server.route("/api/macros/save", POST)
def api_save_macro(request):
    try:
        macro = validate_macro(request.json())
        existing = find_macro(macro["id"])
        if existing:
            stop_task(existing["id"])
            previous = existing.copy()
            existing.clear()
            existing.update(macro)
            try:
                save_macros()
            except (ValueError, OSError):
                existing.clear()
                existing.update(previous)
                raise
        else:
            if len(macros) >= MAX_MACROS:
                raise ValueError("Maximum of 12 macros reached")
            macro["id"] = next_macro_id()
            macros.append(macro)
            try:
                save_macros()
            except (ValueError, OSError):
                macros.remove(macro)
                raise
        return JSONResponse(request, {"ok": True, "macro": macro})
    except (ValueError, TypeError, OSError) as error:
        return JSONResponse(request, {"ok": False, "error": str(error)}, status=(400, "Bad Request"))


@server.route("/api/macros/delete", POST)
def api_delete_macro(request):
    try:
        macro_id = request.json().get("id")
        macro = find_macro(macro_id)
        if not macro:
            raise ValueError("Macro not found")
        stop_task(macro_id)
        index = macros.index(macro)
        macros.remove(macro)
        try:
            save_macros()
        except (ValueError, OSError):
            macros.insert(index, macro)
            raise
        return JSONResponse(request, {"ok": True})
    except (ValueError, AttributeError, OSError) as error:
        return JSONResponse(request, {"ok": False, "error": str(error)}, status=(400, "Bad Request"))


@server.route("/api/macros/control", POST)
def api_control_macro(request):
    try:
        data = request.json()
        macro = find_macro(data.get("id"))
        command = data.get("command")
        if not macro:
            raise ValueError("Macro not found")
        is_active = macro["id"] in active_ids()
        if command == "once" and macro["mode"] == "normal":
            emergency_release_all()
            start_task(macro, False)
        elif command == "start" and macro["mode"] == "hold":
            if not is_active:
                emergency_release_all()
                start_task(macro, True, True)
        elif command == "heartbeat" and macro["mode"] == "hold":
            for task in tasks:
                if task["id"] == macro["id"]:
                    task["lease_until"] = time.monotonic() + HOLD_LEASE_SECONDS
        elif command == "stop":
            stop_task(macro["id"])
        elif command == "toggle" and macro["mode"] == "toggle":
            if is_active:
                stop_task(macro["id"])
            else:
                emergency_release_all()
                start_task(macro, True)
        else:
            raise ValueError("Command does not match macro mode")
        return JSONResponse(request, {"ok": True, "active": macro["id"] in active_ids()})
    except (ValueError, AttributeError) as error:
        return JSONResponse(request, {"ok": False, "error": str(error)}, status=(400, "Bad Request"))


@server.route("/mouse/start")
def mouse_start(request):
    global mouse_active, mouse_lease_until, mouse_arm_started
    now = time.monotonic()
    if not mouse_active:
        emergency_release_all()
        print("Web hold activated: Mouse Turbo")
    mouse_active = True
    mouse_arm_started = now
    mouse_lease_until = now + BUILTIN_HOLD_LEASE_SECONDS
    return Response(request, "OK")


@server.route("/mouse/stop")
def mouse_stop(request):
    global mouse_active, mouse_lease_until, mouse_arm_started
    mouse_active = False
    mouse_lease_until = 0.0
    mouse_arm_started = None
    return Response(request, "OK")


@server.route("/space/start")
def space_start(request):
    global space_active, space_lease_until, space_arm_started
    global next_space_tap
    now = time.monotonic()
    if not space_active:
        emergency_release_all()
        print("Web hold activated: Space Turbo")
    space_active = True
    space_arm_started = now
    next_space_tap = now
    space_lease_until = now + BUILTIN_HOLD_LEASE_SECONDS
    return Response(request, "OK")


@server.route("/space/stop")
def space_stop(request):
    global space_active, space_lease_until, space_arm_started
    space_active = False
    space_lease_until = 0.0
    space_arm_started = None
    return Response(request, "OK")


@server.route("/walk/toggle")
def walk_toggle(request):
    global walk_active
    turning_on = not walk_active
    if turning_on:
        emergency_release_all()
    walk_active = turning_on
    print("Web action: AFK Walk", "ON" if walk_active else "OFF")
    if walk_active:
        if key_refs.get(Keycode.W, 0) == 0:
            keyboard.press(Keycode.W)
        key_refs[Keycode.W] = key_refs.get(Keycode.W, 0) + 1
    else:
        count = key_refs.get(Keycode.W, 1) - 1
        if count <= 0:
            key_refs.pop(Keycode.W, None)
            keyboard.release(Keycode.W)
        else:
            key_refs[Keycode.W] = count
    return Response(request, "OK")


def tap_combo(keycodes, hold_seconds=0.08):
    pressed = []
    for keycode in keycodes:
        if key_refs.get(keycode, 0) == 0:
            keyboard.press(keycode)
            pressed.append(keycode)
    time.sleep(hold_seconds)
    for keycode in reversed(pressed):
        keyboard.release(keycode)


@server.route("/ubuntu")
def ubuntu(request):
    emergency_release_all()
    print("Web action: Ubuntu Terminal")
    tap_combo((Keycode.CONTROL, Keycode.ALT, Keycode.T))
    return Response(request, "OK")


@server.route("/cmd")
def cmd(request):
    emergency_release_all()
    print("Web action: Command Prompt")
    tap_combo((Keycode.WINDOWS, Keycode.R))
    time.sleep(0.15)
    layout.write("cmd")
    tap_combo((Keycode.ENTER,), 0.03)
    return Response(request, "OK")


@server.route("/altf4")
def altf4(request):
    emergency_release_all()
    print("Web action: Alt+F4")
    tap_combo((Keycode.ALT, Keycode.F4))
    return Response(request, "OK")


@server.route("/stop")
def stop(request):
    emergency_release_all()
    return Response(request, "STOPPED")


server_running = False
if connect_saved_network():
    server.start(str(wifi.radio.ipv4_address), port=80)
    server_running = True
    print("Open: http://{}".format(wifi.radio.ipv4_address))
else:
    try:
        wifi.radio.stop_station()
    except Exception:
        pass
    print("No saved network found. WiFi stopped until the next reboot.")
    print("Offline mode: press BOOTSEL to toggle the autoclicker")

wifi_offline_reported = not server_running

while True:
    try:
        now = time.monotonic()
        poll_bootsel_button(now)

        wifi_ready = (wifi.radio.connected and
                      wifi.radio.ipv4_address is not None)

        if wifi_ready:
            wifi_offline_reported = False
            if not server_running:
                server.start(str(wifi.radio.ipv4_address), port=80)
                server_running = True
                print("Open: http://{}".format(wifi.radio.ipv4_address))
            server.poll()
        else:
            if server_running:
                try:
                    server.stop()
                except Exception as error:
                    print("Web server stop:", error)
                server_running = False
            if not wifi_offline_reported:
                print("WiFi lost. No rescan until Ctrl+D or power cycle.")
                wifi_offline_reported = True

        run_macro_engine(now)
        service_builtin_holds(now)
        run_fast_mouse_clicker(time.monotonic())

        led.value = bool(bootsel_click_active or mouse_active or
                         space_active or walk_active or tasks)
    except Exception as error:
        print("Main loop error:", error)
        emergency_release_all()
        time.sleep(0.1)
