#pragma once

#include <cstdint>

bool wifi_manager_init();
void wifi_manager_tick(uint64_t now_ms);
bool wifi_manager_connected();
const char *wifi_manager_ip();

