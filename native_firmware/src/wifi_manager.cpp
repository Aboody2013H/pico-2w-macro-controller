#include "wifi_manager.h"

#include <cstdio>
#include <cstring>

#include "lwip/apps/mdns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "macro_engine.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "wifi_config.h"

namespace {

// Some home routers authenticate quickly but take longer to answer DHCP.
// Negative link states still advance immediately, so this only extends the
// wait when an access point is genuinely reachable.
constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;
constexpr uint64_t LOSS_GRACE_MS = 8000;
constexpr uint64_t RETRY_MS = 5000;
bool connected = false;
bool mdns_added = false;
uint64_t lost_at = 0;
uint64_t next_retry = 0;
uint64_t attempt_deadline = 0;
size_t credential_index = 0;
bool attempt_active = false;
char hostname[32] = "pico2w-macro";

void begin_attempt(uint64_t now_ms) {
    while (credential_index < sizeof(WIFI_CREDENTIALS) / sizeof(WIFI_CREDENTIALS[0]) &&
           (!WIFI_CREDENTIALS[credential_index].ssid[0] || !WIFI_CREDENTIALS[credential_index].password[0])) {
        ++credential_index;
    }
    if (credential_index >= sizeof(WIFI_CREDENTIALS) / sizeof(WIFI_CREDENTIALS[0])) {
        attempt_active = false;
        next_retry = now_ms + RETRY_MS;
        std::printf("No saved WiFi available; retrying in 5 seconds\n");
        return;
    }
    const auto &network = WIFI_CREDENTIALS[credential_index];
    std::printf("Trying WiFi: %s\n", network.ssid);
    int result = cyw43_arch_wifi_connect_async(
        network.ssid, network.password, CYW43_AUTH_WPA2_AES_PSK);
    if (result) {
        std::printf("WiFi start failed for %s (error %d)\n", network.ssid, result);
        ++credential_index;
        begin_attempt(now_ms);
        return;
    }
    attempt_active = true;
    attempt_deadline = now_ms + CONNECT_TIMEOUT_MS;
}

void begin_cycle(uint64_t now_ms) {
    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_sta_mode();
    credential_index = 0;
    begin_attempt(now_ms);
}

const char *status_name(int status) {
    switch (status) {
        case CYW43_LINK_DOWN: return "down";
        case CYW43_LINK_JOIN: return "joining";
        case CYW43_LINK_NOIP: return "waiting for IP";
        case CYW43_LINK_UP: return "up";
        case CYW43_LINK_FAIL: return "failed";
        case CYW43_LINK_NONET: return "network not found";
        case CYW43_LINK_BADAUTH: return "bad password/auth";
        default: return "unknown";
    }
}

void add_mdns() {
    if (mdns_added) return;
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], hostname);
    cyw43_arch_lwip_end();
    mdns_added = true;
}

} // namespace

bool wifi_manager_init() {
    if (cyw43_arch_init()) {
        std::printf("CYW43 initialization failed\n");
        return false;
    }
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], hostname);
    begin_cycle(to_ms_since_boot(get_absolute_time()));
    return true;
}

void wifi_manager_tick(uint64_t now_ms) {
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    static int previous_status = 999;
    if (status != previous_status) {
        std::printf("WiFi status: %s (%d)\n", status_name(status), status);
        previous_status = status;
    }
    bool up = status == CYW43_LINK_UP &&
              !ip4_addr_isany_val(*netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
    if (up) {
        if (!connected) {
            const char *ssid = credential_index < sizeof(WIFI_CREDENTIALS) / sizeof(WIFI_CREDENTIALS[0]) ?
                WIFI_CREDENTIALS[credential_index].ssid : "saved network";
            std::printf("Connected to %s at %s\n", ssid, wifi_manager_ip());
            add_mdns();
            std::printf("Open http://%s or http://%s.local\n", wifi_manager_ip(), hostname);
        }
        connected = true;
        attempt_active = false;
        lost_at = 0;
        return;
    }
    if (connected && !lost_at) {
        lost_at = now_ms;
        std::printf("WiFi lost; allowing automatic reconnect\n");
        macro_stop_all();
    }
    connected = false;
    if (lost_at) {
        if (now_ms - lost_at < LOSS_GRACE_MS) return;
        lost_at = 0;
        attempt_active = false;
        begin_cycle(now_ms);
        return;
    }
    if (attempt_active) {
        if (status < 0 || now_ms >= attempt_deadline) {
            std::printf("WiFi failed for %s (status %d)\n",
                        WIFI_CREDENTIALS[credential_index].ssid, status);
            ++credential_index;
            begin_attempt(now_ms);
        }
        return;
    }
    if (now_ms >= next_retry) begin_cycle(now_ms);
}

bool wifi_manager_connected() { return connected; }

const char *wifi_manager_ip() {
    return ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
}
