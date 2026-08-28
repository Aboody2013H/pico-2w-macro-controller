// Native Pico 2 W BOOTSEL reader for CircuitPython.
// BOOTSEL shares the external flash chip-select line, so the actual sample
// routine must execute entirely from RAM while flash access is paused.

#include "py/obj.h"
#include "py/runtime.h"

#include "hardware/gpio.h"
#include "hardware/regs/io_qspi.h"
#include "hardware/regs/sio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "pico/platform.h"


static bool __no_inline_not_in_flash_func(read_bootsel_button)(void) {
    const uint cs_pin_index = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(
        &ioqspi_hw->io[cs_pin_index].ctrl,
        GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS
    );

    // No flash-resident delay calls are safe until chip select is restored.
    uint32_t start = timer_hw->timerawl;
    while ((uint32_t)(timer_hw->timerawl - start) <= 8u) {
    }

    bool pressed = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);

    hw_write_masked(
        &ioqspi_hw->io[cs_pin_index].ctrl,
        GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS
    );
    restore_interrupts(flags);
    return pressed;
}


static mp_obj_t bootsel_pressed(void) {
    return mp_obj_new_bool(read_bootsel_button());
}
static MP_DEFINE_CONST_FUN_OBJ_0(bootsel_pressed_obj, bootsel_pressed);


static const mp_rom_map_elem_t bootsel_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_bootsel)},
    {MP_ROM_QSTR(MP_QSTR_pressed), MP_ROM_PTR(&bootsel_pressed_obj)},
};
static MP_DEFINE_CONST_DICT(bootsel_module_globals, bootsel_module_globals_table);


const mp_obj_module_t bootsel_user_cmodule = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&bootsel_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_bootsel, bootsel_user_cmodule);
