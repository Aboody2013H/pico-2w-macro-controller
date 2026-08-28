#include "macro_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "pico/time.h"
#include "tusb.h"
#include "usb_descriptors.h"

namespace {

enum class CodeKind : uint8_t { Key, Mouse };
struct CodeInfo { const char *name; CodeKind kind; uint8_t usage; };

#define KEY(name, usage) {name, CodeKind::Key, usage}
#define MOUSE(name, mask) {name, CodeKind::Mouse, mask}
const CodeInfo CODES[] = {
    KEY("A", HID_KEY_A), KEY("B", HID_KEY_B), KEY("C", HID_KEY_C),
    KEY("D", HID_KEY_D), KEY("E", HID_KEY_E), KEY("F", HID_KEY_F),
    KEY("G", HID_KEY_G), KEY("H", HID_KEY_H), KEY("I", HID_KEY_I),
    KEY("J", HID_KEY_J), KEY("K", HID_KEY_K), KEY("L", HID_KEY_L),
    KEY("M", HID_KEY_M), KEY("N", HID_KEY_N), KEY("O", HID_KEY_O),
    KEY("P", HID_KEY_P), KEY("Q", HID_KEY_Q), KEY("R", HID_KEY_R),
    KEY("S", HID_KEY_S), KEY("T", HID_KEY_T), KEY("U", HID_KEY_U),
    KEY("V", HID_KEY_V), KEY("W", HID_KEY_W), KEY("X", HID_KEY_X),
    KEY("Y", HID_KEY_Y), KEY("Z", HID_KEY_Z),
    KEY("0", HID_KEY_0), KEY("1", HID_KEY_1), KEY("2", HID_KEY_2),
    KEY("3", HID_KEY_3), KEY("4", HID_KEY_4), KEY("5", HID_KEY_5),
    KEY("6", HID_KEY_6), KEY("7", HID_KEY_7), KEY("8", HID_KEY_8),
    KEY("9", HID_KEY_9),
    KEY("AA", HID_KEY_BRACKET_LEFT), KEY("AE", HID_KEY_APOSTROPHE),
    KEY("OE", HID_KEY_SEMICOLON), KEY("SECTION", HID_KEY_GRAVE),
    KEY("PLUS", HID_KEY_MINUS), KEY("ACUTE", HID_KEY_EQUAL),
    KEY("DIAERESIS", HID_KEY_BRACKET_RIGHT), KEY("APOSTROPHE", HID_KEY_BACKSLASH),
    KEY("LESS_THAN", HID_KEY_EUROPE_2), KEY("COMMA", HID_KEY_COMMA),
    KEY("PERIOD", HID_KEY_PERIOD), KEY("HYPHEN", HID_KEY_SLASH),
    KEY("CTRL", HID_KEY_CONTROL_LEFT), KEY("SHIFT", HID_KEY_SHIFT_LEFT),
    KEY("ALT", HID_KEY_ALT_LEFT), KEY("ALTGR", HID_KEY_ALT_RIGHT),
    KEY("GUI", HID_KEY_GUI_LEFT), KEY("RCTRL", HID_KEY_CONTROL_RIGHT),
    KEY("RSHIFT", HID_KEY_SHIFT_RIGHT), KEY("SPACE", HID_KEY_SPACE),
    KEY("ENTER", HID_KEY_ENTER), KEY("TAB", HID_KEY_TAB),
    KEY("ESC", HID_KEY_ESCAPE), KEY("BACKSPACE", HID_KEY_BACKSPACE),
    KEY("DELETE", HID_KEY_DELETE), KEY("INSERT", HID_KEY_INSERT),
    KEY("HOME", HID_KEY_HOME), KEY("END", HID_KEY_END),
    KEY("PAGE_UP", HID_KEY_PAGE_UP), KEY("PAGE_DOWN", HID_KEY_PAGE_DOWN),
    KEY("UP", HID_KEY_ARROW_UP), KEY("DOWN", HID_KEY_ARROW_DOWN),
    KEY("LEFT", HID_KEY_ARROW_LEFT), KEY("RIGHT", HID_KEY_ARROW_RIGHT),
    KEY("CAPS_LOCK", HID_KEY_CAPS_LOCK), KEY("NUM_LOCK", HID_KEY_NUM_LOCK),
    KEY("SCROLL_LOCK", HID_KEY_SCROLL_LOCK), KEY("PRINT_SCREEN", HID_KEY_PRINT_SCREEN),
    KEY("PAUSE", HID_KEY_PAUSE), KEY("MENU", HID_KEY_APPLICATION),
    KEY("KP_0", HID_KEY_KEYPAD_0), KEY("KP_1", HID_KEY_KEYPAD_1),
    KEY("KP_2", HID_KEY_KEYPAD_2), KEY("KP_3", HID_KEY_KEYPAD_3),
    KEY("KP_4", HID_KEY_KEYPAD_4), KEY("KP_5", HID_KEY_KEYPAD_5),
    KEY("KP_6", HID_KEY_KEYPAD_6), KEY("KP_7", HID_KEY_KEYPAD_7),
    KEY("KP_8", HID_KEY_KEYPAD_8), KEY("KP_9", HID_KEY_KEYPAD_9),
    KEY("KP_PLUS", HID_KEY_KEYPAD_ADD), KEY("KP_MINUS", HID_KEY_KEYPAD_SUBTRACT),
    KEY("KP_MULTIPLY", HID_KEY_KEYPAD_MULTIPLY), KEY("KP_DIVIDE", HID_KEY_KEYPAD_DIVIDE),
    KEY("KP_PERIOD", HID_KEY_KEYPAD_DECIMAL), KEY("KP_ENTER", HID_KEY_KEYPAD_ENTER),
    KEY("F1", HID_KEY_F1), KEY("F2", HID_KEY_F2), KEY("F3", HID_KEY_F3),
    KEY("F4", HID_KEY_F4), KEY("F5", HID_KEY_F5), KEY("F6", HID_KEY_F6),
    KEY("F7", HID_KEY_F7), KEY("F8", HID_KEY_F8), KEY("F9", HID_KEY_F9),
    KEY("F10", HID_KEY_F10), KEY("F11", HID_KEY_F11), KEY("F12", HID_KEY_F12),
    MOUSE("MOUSE_LEFT", MOUSE_BUTTON_LEFT),
    MOUSE("MOUSE_RIGHT", MOUSE_BUTTON_RIGHT),
    MOUSE("MOUSE_MIDDLE", MOUSE_BUTTON_MIDDLE),
    MOUSE("MOUSE_BACK", MOUSE_BUTTON_BACKWARD),
    MOUSE("MOUSE_FORWARD", MOUSE_BUTTON_FORWARD),
};
#undef KEY
#undef MOUSE

constexpr size_t CODE_COUNT = sizeof(CODES) / sizeof(CODES[0]);
constexpr size_t MAX_TASKS = MAX_MACROS + 1;
constexpr uint64_t HOLD_LEASE_MS = 2500;
constexpr uint64_t TAP_MS = 9;
constexpr uint64_t TURBO_INTERVAL_MS = 24;

struct Task {
    bool used = false;
    bool system = false;
    bool repeating = false;
    bool hold_lease = false;
    uint32_t id = 0;
    const MacroDefinition *macro = nullptr;
    uint8_t index = 0;
    uint64_t due_ms = 0;
    uint64_t lease_until_ms = 0;
    bool releasing_tap = false;
    uint8_t tap_code = 0;
    uint16_t tap_delay_ms = 0;
    bool held[CODE_COUNT]{};
};

Task tasks[MAX_TASKS]{};
MacroDefinition system_macro{};
uint8_t key_refs[256]{};
uint8_t mouse_refs[8]{};
bool mouse_on = false;
bool space_on = false;
bool walk_on = false;
bool mouse_turbo_down = false;
bool space_turbo_down = false;
uint64_t mouse_turbo_due = 0;
uint64_t space_turbo_due = 0;
bool keyboard_dirty = true;
bool mouse_dirty = true;

void key_ref_press(uint8_t usage) {
    if (key_refs[usage] < 255) ++key_refs[usage];
    keyboard_dirty = true;
}

void key_ref_release(uint8_t usage) {
    if (key_refs[usage]) --key_refs[usage];
    keyboard_dirty = true;
}

void mouse_ref_press(uint8_t mask) {
    for (int bit = 0; bit < 5; ++bit) if (mask & (1u << bit)) {
        if (mouse_refs[bit] < 255) ++mouse_refs[bit];
    }
    mouse_dirty = true;
}

void mouse_ref_release(uint8_t mask) {
    for (int bit = 0; bit < 5; ++bit) if ((mask & (1u << bit)) && mouse_refs[bit]) {
        --mouse_refs[bit];
    }
    mouse_dirty = true;
}

void code_press(uint8_t code) {
    if (CODES[code].kind == CodeKind::Mouse) mouse_ref_press(CODES[code].usage);
    else key_ref_press(CODES[code].usage);
}

void code_release(uint8_t code) {
    if (CODES[code].kind == CodeKind::Mouse) mouse_ref_release(CODES[code].usage);
    else key_ref_release(CODES[code].usage);
}

void release_task(Task &task) {
    if (task.releasing_tap) {
        code_release(task.tap_code);
        task.releasing_tap = false;
    }
    for (size_t i = 0; i < CODE_COUNT; ++i) if (task.held[i]) {
        code_release((uint8_t)i);
        task.held[i] = false;
    }
    task.used = false;
}

Task *find_task(uint32_t id) {
    for (auto &task : tasks) if (task.used && !task.system && task.id == id) return &task;
    return nullptr;
}

Task *free_task() {
    for (auto &task : tasks) if (!task.used) return &task;
    return nullptr;
}

void start_task(const MacroDefinition &macro, bool repeating, bool hold_lease, bool system = false) {
    if (!system) macro_stop(macro.id);
    Task *task = free_task();
    if (!task) return;
    *task = Task{};
    task->used = true;
    task->system = system;
    task->repeating = repeating;
    task->hold_lease = hold_lease;
    task->id = macro.id;
    task->macro = &macro;
    task->due_ms = to_ms_since_boot(get_absolute_time());
    task->lease_until_ms = task->due_ms + HOLD_LEASE_MS;
}

bool code_held_by_others(uint8_t code) {
    const auto &info = CODES[code];
    if (info.kind == CodeKind::Key) return key_refs[info.usage] != 0;
    for (int bit = 0; bit < 5; ++bit) if ((info.usage & (1u << bit)) && mouse_refs[bit]) return true;
    return false;
}

void execute_step(Task &task, const MacroStep &step, uint64_t now_ms) {
    if (step.action == MacroAction::Down) {
        if (!task.held[step.code]) {
            code_press(step.code);
            task.held[step.code] = true;
        }
        ++task.index;
        task.due_ms = now_ms + step.delay_ms;
    } else if (step.action == MacroAction::Up) {
        if (task.held[step.code]) {
            code_release(step.code);
            task.held[step.code] = false;
        }
        ++task.index;
        task.due_ms = now_ms + step.delay_ms;
    } else {
        if (!code_held_by_others(step.code)) {
            code_press(step.code);
            task.releasing_tap = true;
            task.tap_code = step.code;
            task.tap_delay_ms = step.delay_ms;
            task.due_ms = now_ms + TAP_MS;
        } else {
            ++task.index;
            task.due_ms = now_ms + step.delay_ms;
        }
    }
}

void turbo_tick(bool active, uint8_t code, bool &down, uint64_t &due, uint64_t now_ms) {
    if (!active) {
        if (down) { code_release(code); down = false; }
        return;
    }
    if (now_ms < due) return;
    if (down) {
        code_release(code);
        down = false;
        due = now_ms + TURBO_INTERVAL_MS / 2;
    } else if (!code_held_by_others(code)) {
        code_press(code);
        down = true;
        due = now_ms + TURBO_INTERVAL_MS / 2;
    } else {
        due = now_ms + TURBO_INTERVAL_MS;
    }
}

void add_system_step(const char *code, MacroAction action, uint16_t delay) {
    if (system_macro.step_count >= MAX_STEPS) return;
    int index = macro_find_code(code);
    if (index < 0) return;
    system_macro.steps[system_macro.step_count++] = {(uint8_t)index, action, delay};
}

void begin_system_macro() {
    for (auto &task : tasks) if (task.used && task.system) release_task(task);
    system_macro = MacroDefinition{};
    system_macro.id = 0xFFFFFFFFu;
    std::strcpy(system_macro.name, "System action");
}

class JsonCursor {
public:
    explicit JsonCursor(const char *text) : p_(text) {}
    void ws() { while (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n') ++p_; }
    bool take(char c) { ws(); if (*p_ != c) return false; ++p_; return true; }
    bool string(char *out, size_t size) {
        ws(); if (*p_++ != '"') return false;
        size_t n = 0;
        while (*p_ && *p_ != '"') {
            unsigned char c = (unsigned char)*p_++;
            if (c == '\\') {
                char e = *p_++;
                if (e == 'n') c = '\n'; else if (e == 'r') c = '\r';
                else if (e == 't') c = '\t'; else if (e == 'b') c = '\b';
                else if (e == 'f') c = '\f'; else if (e == 'u') {
                    for (int i = 0; i < 4 && *p_; ++i) ++p_;
                    c = '?';
                } else c = (unsigned char)e;
            }
            if (n + 1 < size) out[n++] = (char)c;
        }
        if (*p_ != '"') return false;
        ++p_; out[n] = 0; return true;
    }
    bool number(uint32_t &value) {
        ws(); if (*p_ < '0' || *p_ > '9') return false;
        uint64_t v = 0;
        while (*p_ >= '0' && *p_ <= '9') { v = v * 10 + (*p_++ - '0'); if (v > 0xFFFFFFFFu) return false; }
        value = (uint32_t)v; return true;
    }
    bool string_or_null(char *out, size_t size) {
        ws();
        if (std::strncmp(p_, "null", 4) == 0) {
            p_ += 4;
            if (size) out[0] = 0;
            return true;
        }
        return string(out, size);
    }
    bool skip() {
        ws();
        if (*p_ == '"') { char tmp[2]; return string(tmp, sizeof(tmp)); }
        if (*p_ == '{') {
            ++p_; ws(); if (*p_ == '}') { ++p_; return true; }
            for (;;) { char key[2]; if (!string(key, sizeof(key)) || !take(':') || !skip()) return false;
                if (take('}')) return true;
                if (!take(',')) return false;
            }
        }
        if (*p_ == '[') {
            ++p_; ws(); if (*p_ == ']') { ++p_; return true; }
            for (;;) { if (!skip()) return false; if (take(']')) return true; if (!take(',')) return false; }
        }
        if ((*p_ >= '0' && *p_ <= '9') || *p_ == '-') { if (*p_ == '-') ++p_; while (*p_ >= '0' && *p_ <= '9') ++p_; return true; }
        for (const char *word : {"true", "false", "null"}) {
            size_t len = std::strlen(word); if (std::strncmp(p_, word, len) == 0) { p_ += len; return true; }
        }
        return false;
    }
    bool done() { ws(); return *p_ == 0; }
private:
    const char *p_;
};

bool parse_step(JsonCursor &json, MacroStep &step, char *error, size_t error_size) {
    if (!json.take('{')) return false;
    char code[32]{};
    char action[16]{};
    uint32_t delay = 0;
    bool got_code = false, got_action = false, got_delay = false;
    if (json.take('}')) return false;
    for (;;) {
        char key[24];
        if (!json.string(key, sizeof(key)) || !json.take(':')) return false;
        if (!std::strcmp(key, "code")) got_code = json.string(code, sizeof(code));
        else if (!std::strcmp(key, "action")) got_action = json.string(action, sizeof(action));
        else if (!std::strcmp(key, "delay")) got_delay = json.number(delay);
        else if (!json.skip()) return false;
        if (json.take('}')) break;
        if (!json.take(',')) return false;
    }
    int code_index = macro_find_code(code);
    if (!got_code || code_index < 0) { std::snprintf(error, error_size, "Unsupported key"); return false; }
    bool mouse = macro_code_is_mouse((uint8_t)code_index);
    MacroAction parsed;
    if (!std::strcmp(action, mouse ? "click" : "tap")) parsed = MacroAction::Tap;
    else if (!std::strcmp(action, "down")) parsed = MacroAction::Down;
    else if (!std::strcmp(action, "up")) parsed = MacroAction::Up;
    else { std::snprintf(error, error_size, "Unsupported action"); return false; }
    if (!got_action || !got_delay || delay > MAX_DELAY_MS) { std::snprintf(error, error_size, "Invalid step delay"); return false; }
    step = {(uint8_t)code_index, parsed, (uint16_t)delay};
    return true;
}

} // namespace

MacroCollection g_macros{};

void macro_engine_init() {
    keyboard_dirty = mouse_dirty = true;
}

void macro_engine_tick(uint64_t now_ms) {
    turbo_tick(mouse_on, (uint8_t)macro_find_code("MOUSE_LEFT"), mouse_turbo_down, mouse_turbo_due, now_ms);
    turbo_tick(space_on, (uint8_t)macro_find_code("SPACE"), space_turbo_down, space_turbo_due, now_ms);
    for (auto &task : tasks) {
        if (!task.used || now_ms < task.due_ms) continue;
        if (task.hold_lease && now_ms > task.lease_until_ms) { release_task(task); continue; }
        if (task.releasing_tap) {
            code_release(task.tap_code);
            task.releasing_tap = false;
            ++task.index;
            task.due_ms = now_ms + task.tap_delay_ms;
            continue;
        }
        if (task.index >= task.macro->step_count) {
            for (size_t i = 0; i < CODE_COUNT; ++i) if (task.held[i]) {
                code_release((uint8_t)i); task.held[i] = false;
            }
            if (task.repeating) { task.index = 0; task.due_ms = now_ms + task.macro->repeat_ms; }
            else task.used = false;
            continue;
        }
        execute_step(task, task.macro->steps[task.index], now_ms);
    }
}

void macro_hid_tick() {
    if (!tud_mounted() || !tud_hid_ready()) return;
    if (keyboard_dirty) {
        hid_keyboard_report_t report{};
        for (int usage = 0xE0; usage <= 0xE7; ++usage) if (key_refs[usage]) report.modifier |= 1u << (usage - 0xE0);
        int slot = 0;
        for (int usage = 1; usage < 0xE0 && slot < 6; ++usage) if (key_refs[usage]) report.keycode[slot++] = (uint8_t)usage;
        if (tud_hid_report(REPORT_ID_KEYBOARD, &report, sizeof(report))) keyboard_dirty = false;
    } else if (mouse_dirty) {
        uint8_t buttons = 0;
        for (int bit = 0; bit < 5; ++bit) if (mouse_refs[bit]) buttons |= 1u << bit;
        hid_mouse_report_t report{buttons, 0, 0, 0, 0};
        if (tud_hid_report(REPORT_ID_MOUSE, &report, sizeof(report))) mouse_dirty = false;
    }
}

void macro_stop_all() {
    mouse_on = space_on = walk_on = false;
    mouse_turbo_down = space_turbo_down = false;
    for (auto &task : tasks) if (task.used) release_task(task);
    std::memset(key_refs, 0, sizeof(key_refs));
    std::memset(mouse_refs, 0, sizeof(mouse_refs));
    keyboard_dirty = mouse_dirty = true;
}

void builtin_mouse_set(bool active) { mouse_on = active; if (!active) mouse_turbo_due = 0; }
void builtin_space_set(bool active) { space_on = active; if (!active) space_turbo_due = 0; }
void builtin_walk_toggle() {
    int code = macro_find_code("W");
    walk_on = !walk_on;
    if (walk_on) code_press((uint8_t)code); else code_release((uint8_t)code);
}
bool builtin_mouse_active() { return mouse_on; }
bool builtin_space_active() { return space_on; }
bool builtin_walk_active() { return walk_on; }

bool macro_is_active(uint32_t id) { return find_task(id) != nullptr; }
void macro_stop(uint32_t id) { if (Task *task = find_task(id)) release_task(*task); }
int macro_find_index(uint32_t id) {
    for (uint8_t i = 0; i < g_macros.count; ++i) if (g_macros.items[i].id == id) return i;
    return -1;
}

bool macro_control(uint32_t id, const char *command, char *error, size_t error_size) {
    int index = macro_find_index(id);
    if (index < 0) { std::snprintf(error, error_size, "Macro not found"); return false; }
    MacroDefinition &macro = g_macros.items[index];
    if (!std::strcmp(command, "once") && macro.mode == MacroMode::Normal) start_task(macro, false, false);
    else if (!std::strcmp(command, "start") && macro.mode == MacroMode::Hold) start_task(macro, true, true);
    else if (!std::strcmp(command, "heartbeat") && macro.mode == MacroMode::Hold) {
        if (Task *task = find_task(id)) task->lease_until_ms = to_ms_since_boot(get_absolute_time()) + HOLD_LEASE_MS;
    } else if (!std::strcmp(command, "stop")) macro_stop(id);
    else if (!std::strcmp(command, "toggle") && macro.mode == MacroMode::Toggle) {
        if (macro_is_active(id)) macro_stop(id); else start_task(macro, true, false);
    } else { std::snprintf(error, error_size, "Command does not match macro mode"); return false; }
    return true;
}

void macro_launch_ubuntu() {
    begin_system_macro();
    add_system_step("CTRL", MacroAction::Down, 0); add_system_step("ALT", MacroAction::Down, 0);
    add_system_step("T", MacroAction::Tap, 0); add_system_step("ALT", MacroAction::Up, 0);
    add_system_step("CTRL", MacroAction::Up, 0); start_task(system_macro, false, false, true);
}
void macro_launch_cmd() {
    begin_system_macro();
    add_system_step("GUI", MacroAction::Down, 0); add_system_step("R", MacroAction::Tap, 0);
    add_system_step("GUI", MacroAction::Up, 150); add_system_step("C", MacroAction::Tap, 0);
    add_system_step("M", MacroAction::Tap, 0); add_system_step("D", MacroAction::Tap, 0);
    add_system_step("ENTER", MacroAction::Tap, 0); start_task(system_macro, false, false, true);
}
void macro_launch_alt_f4() {
    begin_system_macro();
    add_system_step("ALT", MacroAction::Down, 0); add_system_step("F4", MacroAction::Tap, 0);
    add_system_step("ALT", MacroAction::Up, 0); start_task(system_macro, false, false, true);
}

int macro_find_code(const char *name) {
    for (size_t i = 0; i < CODE_COUNT; ++i) if (!std::strcmp(CODES[i].name, name)) return (int)i;
    return -1;
}
const char *macro_code_name(uint8_t code) { return code < CODE_COUNT ? CODES[code].name : "?"; }
bool macro_code_valid(uint8_t code) { return code < CODE_COUNT; }
bool macro_code_is_mouse(uint8_t code) { return code < CODE_COUNT && CODES[code].kind == CodeKind::Mouse; }
const char *macro_mode_name(MacroMode mode) {
    return mode == MacroMode::Hold ? "hold" : mode == MacroMode::Toggle ? "toggle" : "normal";
}
const char *macro_action_name(MacroAction action, bool mouse) {
    if (action == MacroAction::Down) return "down";
    if (action == MacroAction::Up) return "up";
    return mouse ? "click" : "tap";
}

bool macro_parse_json(const char *text, MacroDefinition &result, char *error, size_t error_size) {
    JsonCursor json(text);
    result = MacroDefinition{};
    char id[20]{}, mode[16]{};
    bool got_name = false, got_mode = false, got_repeat = false, got_steps = false;
    if (!json.take('{')) goto invalid;
    if (json.take('}')) goto invalid;
    for (;;) {
        char key[24];
        if (!json.string(key, sizeof(key)) || !json.take(':')) goto invalid;
        if (!std::strcmp(key, "id")) { if (!json.string_or_null(id, sizeof(id))) goto invalid; }
        else if (!std::strcmp(key, "name")) { if (!json.string(result.name, sizeof(result.name))) goto invalid; got_name = result.name[0]; }
        else if (!std::strcmp(key, "mode")) { if (!json.string(mode, sizeof(mode))) goto invalid; got_mode = true; }
        else if (!std::strcmp(key, "repeat")) { if (!json.number(result.repeat_ms)) goto invalid; got_repeat = true; }
        else if (!std::strcmp(key, "steps")) {
            if (!json.take('[')) goto invalid;
            got_steps = true;
            if (!json.take(']')) for (;;) {
                if (result.step_count >= MAX_STEPS) { std::snprintf(error, error_size, "Maximum of 32 steps reached"); return false; }
                if (!parse_step(json, result.steps[result.step_count], error, error_size)) return false;
                ++result.step_count;
                if (json.take(']')) break;
                if (!json.take(',')) goto invalid;
            }
        } else if (!json.skip()) goto invalid;
        if (json.take('}')) break;
        if (!json.take(',')) goto invalid;
    }
    if (!json.done() || !got_name || !got_mode || !got_repeat || !got_steps || !result.step_count) goto invalid;
    if (result.repeat_ms > MAX_REPEAT_MS) { std::snprintf(error, error_size, "Repeat delay is too large"); return false; }
    if (!std::strcmp(mode, "normal")) result.mode = MacroMode::Normal;
    else if (!std::strcmp(mode, "hold")) result.mode = MacroMode::Hold;
    else if (!std::strcmp(mode, "toggle")) result.mode = MacroMode::Toggle;
    else { std::snprintf(error, error_size, "Invalid execution mode"); return false; }
    if (id[0] == 'm') result.id = (uint32_t)std::strtoul(id + 1, nullptr, 10);
    return true;
invalid:
    std::snprintf(error, error_size, "Invalid macro JSON");
    return false;
}
