#include "web_api.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/pbuf.h"
#include "macro_engine.h"
#include "storage.h"

extern volatile bool g_bootsel_pressed;

namespace {

constexpr size_t REQUEST_CAPACITY = 16 * 1024;
char request_body[REQUEST_CAPACITY + 1]{};
size_t request_length = 0;
void *post_connection = nullptr;
char post_uri[40]{};
char post_result[768] = "{\"ok\":false,\"error\":\"No response\"}";

class Writer {
public:
    Writer(char *data, size_t capacity) : data_(data), capacity_(capacity) {}
    void add(const char *text) {
        size_t n = std::strlen(text);
        if (length_ + n >= capacity_) { ok_ = false; return; }
        std::memcpy(data_ + length_, text, n); length_ += n; data_[length_] = 0;
    }
    void format(const char *format, ...) {
        if (!ok_ || length_ >= capacity_) return;
        va_list args; va_start(args, format);
        int n = std::vsnprintf(data_ + length_, capacity_ - length_, format, args);
        va_end(args);
        if (n < 0 || (size_t)n >= capacity_ - length_) { ok_ = false; return; }
        length_ += (size_t)n;
    }
    void json_string(const char *text) {
        add("\"");
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p && ok_; ++p) {
            char escaped[8]{};
            if (*p == '"' || *p == '\\') { escaped[0] = '\\'; escaped[1] = (char)*p; add(escaped); }
            else if (*p == '\n') add("\\n");
            else if (*p == '\r') add("\\r");
            else if (*p == '\t') add("\\t");
            else if (*p < 0x20) { std::snprintf(escaped, sizeof(escaped), "\\u%04x", *p); add(escaped); }
            else { escaped[0] = (char)*p; add(escaped); }
        }
        add("\"");
    }
    size_t length() const { return length_; }
    bool ok() const { return ok_; }
private:
    char *data_;
    size_t capacity_;
    size_t length_ = 0;
    bool ok_ = true;
};

void response_error(const char *message) {
    Writer out(post_result, sizeof(post_result));
    out.add("{\"ok\":false,\"error\":"); out.json_string(message); out.add("}");
}

bool json_string_field(const char *json, const char *field, char *out, size_t out_size) {
    char needle[48];
    std::snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char *p = std::strstr(json, needle);
    if (!p) return false;
    p += std::strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != '"') return false;
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) c = *p++;
        if (n + 1 < out_size) out[n++] = c;
    }
    if (*p != '"') return false;
    out[n] = 0;
    return true;
}

uint32_t parse_macro_id(const char *text) {
    return text && text[0] == 'm' ? (uint32_t)std::strtoul(text + 1, nullptr, 10) : 0;
}

void save_macro_post() {
    MacroDefinition macro;
    char error[128];
    if (!macro_parse_json(request_body, macro, error, sizeof(error))) { response_error(error); return; }
    MacroCollection previous = g_macros;
    int existing = macro.id ? macro_find_index(macro.id) : -1;
    if (existing >= 0) {
        macro_stop(macro.id);
        g_macros.items[existing] = macro;
    } else {
        if (g_macros.count >= MAX_MACROS) { response_error("Maximum of 12 macros reached"); return; }
        macro.id = g_macros.next_id++;
        g_macros.items[g_macros.count++] = macro;
    }
    if (!storage_save()) { g_macros = previous; response_error("Could not save macro to flash"); return; }
    std::snprintf(post_result, sizeof(post_result), "{\"ok\":true,\"macro\":{\"id\":\"m%lu\"}}",
                  (unsigned long)macro.id);
}

void delete_macro_post() {
    char id_text[20];
    if (!json_string_field(request_body, "id", id_text, sizeof(id_text))) { response_error("Invalid macro id"); return; }
    uint32_t id = parse_macro_id(id_text);
    int index = macro_find_index(id);
    if (index < 0) { response_error("Macro not found"); return; }
    MacroCollection previous = g_macros;
    macro_stop(id);
    for (uint8_t i = (uint8_t)index; i + 1 < g_macros.count; ++i) g_macros.items[i] = g_macros.items[i + 1];
    --g_macros.count;
    if (!storage_save()) { g_macros = previous; response_error("Could not update flash storage"); return; }
    std::strcpy(post_result, "{\"ok\":true}");
}

void control_macro_post() {
    char id_text[20], command[20], error[128];
    if (!json_string_field(request_body, "id", id_text, sizeof(id_text)) ||
        !json_string_field(request_body, "command", command, sizeof(command))) {
        response_error("Invalid macro command"); return;
    }
    uint32_t id = parse_macro_id(id_text);
    if (!macro_control(id, command, error, sizeof(error))) { response_error(error); return; }
    std::snprintf(post_result, sizeof(post_result), "{\"ok\":true,\"active\":%s}",
                  macro_is_active(id) ? "true" : "false");
}

void dispatch_post(const char *uri) {
    if (!std::strcmp(uri, "/api/macros/save")) save_macro_post();
    else if (!std::strcmp(uri, "/api/macros/delete")) delete_macro_post();
    else if (!std::strcmp(uri, "/api/macros/control")) control_macro_post();
    else response_error("Unknown API endpoint");
}

void write_macro_json(Writer &out, const MacroDefinition &macro) {
    out.format("{\"id\":\"m%lu\",\"name\":", (unsigned long)macro.id);
    out.json_string(macro.name);
    out.add(",\"mode\":"); out.json_string(macro_mode_name(macro.mode));
    out.format(",\"repeat\":%lu,\"steps\":[", (unsigned long)macro.repeat_ms);
    for (uint8_t i = 0; i < macro.step_count; ++i) {
        if (i) out.add(",");
        const auto &step = macro.steps[i];
        const char *code = macro_code_name(step.code);
        bool mouse = macro_code_is_mouse(step.code);
        out.add("{\"device\":"); out.json_string(mouse ? "mouse" : "key");
        out.add(",\"code\":"); out.json_string(code);
        out.add(",\"label\":"); out.json_string(code);
        out.add(",\"action\":"); out.json_string(macro_action_name(step.action, mouse));
        out.format(",\"delay\":%u}", step.delay_ms);
    }
    out.add("]}");
}

bool make_http_json(struct fs_file *file, const char *name) {
    size_t body_capacity = !std::strcmp(name, "/api/macros") ? 48 * 1024 : 4 * 1024;
    constexpr size_t prefix = 256;
    char *memory = static_cast<char *>(std::malloc(body_capacity + prefix));
    if (!memory) return false;
    Writer body(memory + prefix, body_capacity);
    if (!std::strcmp(name, "/api/macros")) {
        body.add("{\"macros\":[");
        for (uint8_t i = 0; i < g_macros.count; ++i) { if (i) body.add(","); write_macro_json(body, g_macros.items[i]); }
        body.add("]}");
    } else if (!std::strcmp(name, "/api/status")) {
        body.add("{\"active\":[");
        bool first = true;
        for (uint8_t i = 0; i < g_macros.count; ++i) if (macro_is_active(g_macros.items[i].id)) {
            if (!first) {
                body.add(",");
            }
            first = false;
            body.format("\"m%lu\"", (unsigned long)g_macros.items[i].id);
        }
        body.format("],\"mouse\":%s,\"space\":%s,\"walk\":%s,\"bootsel\":%s}",
                    builtin_mouse_active() ? "true" : "false",
                    builtin_space_active() ? "true" : "false",
                    builtin_walk_active() ? "true" : "false",
                    g_bootsel_pressed ? "true" : "false");
    } else if (!std::strcmp(name, "/api-response.json")) body.add(post_result);
    else { std::free(memory); return false; }

    if (!body.ok()) { std::free(memory); return false; }
    char header[prefix];
    int header_length = std::snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
        (unsigned)body.length());
    std::memmove(memory + header_length, memory + prefix, body.length());
    std::memcpy(memory, header, (size_t)header_length);
    file->data = memory;
    file->len = header_length + (int)body.length();
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT;
    file->pextension = memory;
    return true;
}

const char *cgi_handler(int index, int, char **, char **) {
    switch (index) {
        case 0: builtin_mouse_set(true); break;
        case 1: builtin_mouse_set(false); break;
        case 2: builtin_space_set(true); break;
        case 3: builtin_space_set(false); break;
        case 4: builtin_walk_toggle(); break;
        case 5: macro_launch_ubuntu(); break;
        case 6: macro_launch_cmd(); break;
        case 7: macro_launch_alt_f4(); break;
        case 8: macro_stop_all(); break;
    }
    return "/ok.txt";
}

tCGI cgi_handlers[] = {
    {"/mouse/start", cgi_handler}, {"/mouse/stop", cgi_handler},
    {"/space/start", cgi_handler}, {"/space/stop", cgi_handler},
    {"/walk/toggle", cgi_handler}, {"/ubuntu", cgi_handler},
    {"/cmd", cgi_handler}, {"/altf4", cgi_handler}, {"/stop", cgi_handler},
};

} // namespace

extern "C" int fs_open_custom(struct fs_file *file, const char *name) {
    return make_http_json(file, name) ? 1 : 0;
}

extern "C" void fs_close_custom(struct fs_file *file) {
    std::free(file->pextension);
    file->pextension = nullptr;
}

extern "C" err_t httpd_post_begin(void *connection, const char *uri, const char *,
                                    u16_t, int content_len, char *response_uri,
                                    u16_t response_uri_len, u8_t *post_auto_wnd) {
    if (post_connection || content_len < 0 || (size_t)content_len > REQUEST_CAPACITY ||
        (std::strcmp(uri, "/api/macros/save") && std::strcmp(uri, "/api/macros/delete") &&
         std::strcmp(uri, "/api/macros/control"))) return ERR_VAL;
    post_connection = connection;
    std::snprintf(post_uri, sizeof(post_uri), "%s", uri);
    request_length = 0;
    request_body[0] = 0;
    std::snprintf(response_uri, response_uri_len, "/api-response.json");
    *post_auto_wnd = 1;
    return ERR_OK;
}

extern "C" err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    if (connection != post_connection || request_length + p->tot_len > REQUEST_CAPACITY) {
        pbuf_free(p); return ERR_VAL;
    }
    pbuf_copy_partial(p, request_body + request_length, p->tot_len, 0);
    request_length += p->tot_len;
    request_body[request_length] = 0;
    pbuf_free(p);
    return ERR_OK;
}

extern "C" void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    if (connection == post_connection) {
        dispatch_post(post_uri);
        std::snprintf(response_uri, response_uri_len, "/api-response.json");
    }
    post_connection = nullptr;
    post_uri[0] = 0;
}

void web_api_init() {
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, sizeof(cgi_handlers) / sizeof(cgi_handlers[0]));
}
