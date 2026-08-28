#include <cstdio>

#include "macro_engine.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "storage.h"
#include "tusb.h"
#include "web_api.h"
#include "wifi_manager.h"

volatile bool g_bootsel_pressed = false;

int main() {
    stdio_init_all();
    if (!tud_init(0)) return 1;

    macro_engine_init();
    bool restored = storage_load();
    std::printf("Pico 2W Macro Native starting (%u macros%s)\n",
                g_macros.count, restored ? " restored" : "");

    if (!wifi_manager_init()) return 2;
    web_api_init();

    bool led_active = false;
    while (true) {
        tud_task();
        uint64_t now_ms = to_ms_since_boot(get_absolute_time());
        wifi_manager_tick(now_ms);
        macro_engine_tick(now_ms);
        macro_hid_tick();
        bool active = builtin_mouse_active() || builtin_space_active() ||
                      builtin_walk_active();
        if (!active) for (uint8_t i = 0; i < g_macros.count; ++i) {
            if (macro_is_active(g_macros.items[i].id)) { active = true; break; }
        }
        if (active != led_active) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, active ? 1 : 0);
            led_active = active;
        }
        sleep_ms(1);
    }
}
