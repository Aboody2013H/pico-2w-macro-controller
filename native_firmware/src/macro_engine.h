#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t MAX_MACROS = 12;
constexpr size_t MAX_STEPS = 32;
constexpr uint32_t MAX_DELAY_MS = 60000;
constexpr uint32_t MAX_REPEAT_MS = 600000;

enum class MacroMode : uint8_t { Normal, Hold, Toggle };
enum class MacroAction : uint8_t { Tap, Down, Up };

struct MacroStep {
    uint8_t code = 0;
    MacroAction action = MacroAction::Tap;
    uint16_t delay_ms = 0;
};

struct MacroDefinition {
    uint32_t id = 0;
    char name[33]{};
    MacroMode mode = MacroMode::Normal;
    uint32_t repeat_ms = 0;
    uint8_t step_count = 0;
    MacroStep steps[MAX_STEPS]{};
};

struct MacroCollection {
    uint32_t next_id = 1;
    uint8_t count = 0;
    MacroDefinition items[MAX_MACROS]{};
};

extern MacroCollection g_macros;

void macro_engine_init();
void macro_engine_tick(uint64_t now_ms);
void macro_hid_tick();
void macro_stop_all();

void builtin_mouse_set(bool active);
void builtin_space_set(bool active);
void builtin_walk_toggle();
bool builtin_mouse_active();
bool builtin_space_active();
bool builtin_walk_active();

bool macro_is_active(uint32_t id);
bool macro_control(uint32_t id, const char *command, char *error, size_t error_size);
void macro_stop(uint32_t id);
void macro_launch_ubuntu();
void macro_launch_cmd();
void macro_launch_alt_f4();

int macro_find_index(uint32_t id);
int macro_find_code(const char *name);
const char *macro_code_name(uint8_t code);
bool macro_code_valid(uint8_t code);
bool macro_code_is_mouse(uint8_t code);
const char *macro_mode_name(MacroMode mode);
const char *macro_action_name(MacroAction action, bool mouse);

bool macro_parse_json(const char *json, MacroDefinition &result,
                      char *error, size_t error_size);
