#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "usb_descriptors.h"

#ifdef AUTOCLICKER_PICO2W
#include "pico/cyw43_arch.h"
#endif

namespace {

constexpr uint32_t BUTTON_SAMPLE_MS = 10;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
// Full-speed USB frames are 1 ms. A complete click needs one button-down and
// one button-up report, making 2 ms (about 500 CPS) the practical USB maximum.
constexpr uint32_t CLICK_HALF_PERIOD_MS = 1;

bool autoclick_enabled = false;
bool mouse_down = false;
uint64_t next_click_transition = 0;
#ifdef AUTOCLICKER_PICO2W
bool wireless_led_ready = false;
#endif

// BOOTSEL is wired to the external flash chip-select line. This function must
// execute from RAM while that line is temporarily floated.
bool __no_inline_not_in_flash_func(read_bootsel_button)() {
    constexpr uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Do not call flash-resident delay functions while flash access is disabled.
    uint32_t start = timer_hw->timerawl;
    while ((uint32_t)(timer_hw->timerawl - start) <= 8u) {}

#if PICO_RP2040
    constexpr uint32_t CS_BIT = 1u << CS_PIN_INDEX;
#else
    constexpr uint32_t CS_BIT = SIO_GPIO_HI_IN_QSPI_CSN_BITS;
#endif
    bool pressed = !(sio_hw->gpio_hi_in & CS_BIT);

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);
    return pressed;
}

void set_autoclick(bool enabled, uint64_t now_ms) {
    autoclick_enabled = enabled;
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, enabled);
#endif
#ifdef AUTOCLICKER_PICO2W
    if (wireless_led_ready) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, enabled);
    }
#endif
    next_click_transition = now_ms;
}

void button_tick(uint64_t now_ms) {
    static uint64_t next_sample = 0;
    static uint64_t changed_at = 0;
    static bool raw = false;
    static bool stable = false;

    if (now_ms < next_sample) return;
    next_sample = now_ms + BUTTON_SAMPLE_MS;

    bool sample = read_bootsel_button();
    if (sample != raw) {
        raw = sample;
        changed_at = now_ms;
    }

    if (raw != stable && now_ms - changed_at >= BUTTON_DEBOUNCE_MS) {
        stable = raw;
        if (stable) set_autoclick(!autoclick_enabled, now_ms);
    }
}

void clicker_tick(uint64_t now_ms) {
    if (!tud_mounted() || !tud_hid_ready()) return;

    if (!autoclick_enabled) {
        if (mouse_down && tud_hid_mouse_report(REPORT_ID_MOUSE, 0, 0, 0, 0, 0)) {
            mouse_down = false;
        }
        return;
    }

    if (now_ms < next_click_transition) return;
    bool next_down = !mouse_down;
    uint8_t buttons = next_down ? MOUSE_BUTTON_LEFT : 0;
    if (tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, 0, 0, 0, 0)) {
        mouse_down = next_down;
        next_click_transition = now_ms + CLICK_HALF_PERIOD_MS;
    }
}

} // namespace

int main() {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
#endif
#ifdef AUTOCLICKER_PICO2W
    wireless_led_ready = cyw43_arch_init() == 0;
    if (wireless_led_ready) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }
#endif

    if (!tud_init(0)) return 1;

    while (true) {
        tud_task();
        uint64_t now_ms = to_ms_since_boot(get_absolute_time());
        button_tick(now_ms);
        clicker_tick(now_ms);
        tight_loop_contents();
    }
}
