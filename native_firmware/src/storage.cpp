#include "storage.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "macro_engine.h"
#include "pico/stdlib.h"

namespace {

constexpr uint32_t STORAGE_MAGIC = 0x314E4D50u; // PMN1
constexpr uint32_t STORAGE_VERSION = 1;
constexpr uint32_t SLOT_SIZE = FLASH_SECTOR_SIZE;
constexpr uint32_t STORAGE_BASE = PICO_FLASH_SIZE_BYTES - (64u * 1024u);

struct StoredData {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc;
    MacroCollection macros;
};

static_assert(sizeof(StoredData) <= SLOT_SIZE, "Macro storage must fit one flash sector");

uint32_t current_sequence = 0;

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    while (length--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

const StoredData *slot_data(unsigned slot) {
    return reinterpret_cast<const StoredData *>(XIP_BASE + STORAGE_BASE + slot * SLOT_SIZE);
}

bool valid(const StoredData *data) {
    if (data->magic != STORAGE_MAGIC || data->version != STORAGE_VERSION ||
        data->payload_size != sizeof(MacroCollection) || data->macros.count > MAX_MACROS) return false;
    if (crc32(reinterpret_cast<const uint8_t *>(&data->macros), sizeof(data->macros)) != data->crc) return false;
    for (uint8_t m = 0; m < data->macros.count; ++m) {
        const auto &macro = data->macros.items[m];
        if (!macro.id || !macro.name[0] || macro.step_count == 0 || macro.step_count > MAX_STEPS ||
            macro.repeat_ms > MAX_REPEAT_MS) return false;
        for (uint8_t s = 0; s < macro.step_count; ++s) {
            if (!macro_code_valid(macro.steps[s].code) || macro.steps[s].delay_ms > MAX_DELAY_MS) return false;
        }
    }
    return true;
}

} // namespace

bool storage_load() {
    const StoredData *a = slot_data(0);
    const StoredData *b = slot_data(1);
    bool a_ok = valid(a), b_ok = valid(b);
    if (!a_ok && !b_ok) {
        g_macros = MacroCollection{};
        return false;
    }
    const StoredData *chosen = !a_ok ? b : !b_ok ? a :
        ((int32_t)(b->sequence - a->sequence) > 0 ? b : a);
    std::memcpy(&g_macros, &chosen->macros, sizeof(g_macros));
    current_sequence = chosen->sequence;
    return true;
}

bool storage_save() {
    alignas(FLASH_PAGE_SIZE) static uint8_t image[SLOT_SIZE];
    std::memset(image, 0xFF, sizeof(image));
    auto *data = reinterpret_cast<StoredData *>(image);
    data->magic = STORAGE_MAGIC;
    data->version = STORAGE_VERSION;
    data->sequence = ++current_sequence;
    data->payload_size = sizeof(MacroCollection);
    std::memcpy(&data->macros, &g_macros, sizeof(g_macros));
    data->crc = crc32(reinterpret_cast<const uint8_t *>(&data->macros), sizeof(data->macros));

    uint32_t slot = data->sequence & 1u;
    uint32_t offset = STORAGE_BASE + slot * SLOT_SIZE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, SLOT_SIZE);
    flash_range_program(offset, image, SLOT_SIZE);
    restore_interrupts(irq);
    return valid(slot_data(slot));
}

void storage_factory_reset() {
    macro_stop_all();
    g_macros = MacroCollection{};
    current_sequence = 0;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(STORAGE_BASE, SLOT_SIZE * 2);
    restore_interrupts(irq);
}
