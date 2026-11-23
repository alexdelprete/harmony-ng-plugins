/* Sony Bravia TV (2013 and later) over its IP control API: plain HTTP with the TV's
 * pre-shared key (TV: Settings > Network > Home network > IP control: Authentication
 * "Normal and Pre-Shared Key", and a key). Remote keys as IRCC codes (SOAP), power, inputs and
 * text as JSON-RPC (/sony/system, /sony/avContent, /sony/appControl). Wake-on-LAN for
 * PowerOn while the TV is off the network. One request per key, no connection kept. */
#include "plugin.h"
#include "wol.h"

#define ID "sony-bravia"
#define HTTP_MS 1500
#ifndef BRAVIA_PORT
#define BRAVIA_PORT 80
#endif

static const char *s_data;

/* An IRCC code is the base64 of 13 bytes: 00 00 00 <kind> 00 00 00 <system> 00 00 00 <cmd> 03
 * (kind 1 for the 12-bit TV codes of system 1, 2 for the 15-bit codes of other systems) */
typedef struct { const char *name; unsigned char sys, cmd; } ircc_t;
static const ircc_t s_ircc[] = {
    {"1", 1, 0x00}, {"2", 1, 0x01}, {"3", 1, 0x02}, {"4", 1, 0x03}, {"5", 1, 0x04}, {"6", 1, 0x05},
    {"7", 1, 0x06}, {"8", 1, 0x07}, {"9", 1, 0x08}, {"0", 1, 0x09}, {"ChannelUp", 1, 0x10},
    {"ChannelDown", 1, 0x11}, {"VolumeUp", 1, 0x12}, {"VolumeDown", 1, 0x13}, {"VolumeMute", 1, 0x14},
    {"Mute", 1, 0x14}, {"PowerToggle", 1, 0x15}, {"Power", 1, 0x15}, {"Input", 1, 0x25},
    {"Info", 1, 0x3A}, {"Display", 1, 0x3A}, {"Home", 1, 0x60}, {"Menu", 1, 0x60}, {"Exit", 1, 0x63},
    {"Select", 1, 0x65}, {"OK", 1, 0x65}, {"Enter", 1, 0x65}, {"Right", 1, 0x33}, {"DirectionRight", 1, 0x33},
    {"Left", 1, 0x34}, {"DirectionLeft", 1, 0x34}, {"Up", 1, 0x74}, {"DirectionUp", 1, 0x74},
    {"Down", 1, 0x75}, {"DirectionDown", 1, 0x75}, {"LastChannel", 1, 0x3B}, {"Jump", 1, 0x3B},
    {"Back", 0x97, 0x23}, {"Return", 0x97, 0x23}, {"Play", 0x97, 0x1A}, {"Pause", 0x97, 0x19},
    {"Stop", 0x97, 0x18}, {"FastForward", 0x97, 0x1C}, {"Fwd", 0x97, 0x1C}, {"Rewind", 0x97, 0x1B},
    {"Rev", 0x97, 0x1B}, {"Next", 0x97, 0x3D}, {"Previous", 0x97, 0x3C}, {"Prev", 0x97, 0x3C},
    {"Record", 0x97, 0x20}, {"Options", 0x97, 0x36}, {"Red", 0x97, 0x25}, {"Green", 0x97, 0x26},
    {"Yellow", 0x97, 0x27}, {"Blue", 0x97, 0x24}, {"Captions", 0x97, 0x28}, {"Subtitles", 0x97, 0x28},
    {"Netflix", 0x1A, 0x7C}, {"Guide", 0xA4, 0x5B}, {"ActionMenu", 0xC4, 0x4B},
    {NULL, 0, 0}
};

static const ircc_t *find_ircc(const char *name) {
    for (const ircc_t *k = s_ircc; k->name; k++) if (plg_streq(k->name, name)) return k;
    return NULL;
}

static void ircc_code(const ircc_t *k, char *out, size_t sz) {
    unsigned char b[13] = {0, 0, 0, (unsigned char)(k->sys == 1 ? 1 : 2), 0, 0, 0, k->sys, 0, 0, 0, k->cmd, 3};
    plg_base64(b, sizeof(b), out, sz);
}

static int post(const char *host, const char *psk, const char *path, const char *extra_headers,
                const char *body, char *resp, size_t resp_sz) {
    char hdr[512], esc[128];
    /* the key goes in a header line: no CR or LF from the config */
    size_t i = 0;
    for (; psk[i] && psk[i] != '\r' && psk[i] != '\n' && i + 1 < sizeof(esc); i++) esc[i] = psk[i];
    esc[i] = '\0';
    snprintf(hdr, sizeof(hdr), "X-Auth-PSK: %s\r\n%s", esc, extra_headers);
    int code = plg_http(host, BRAVIA_PORT, "POST", path, hdr, body, resp, resp_sz, HTTP_MS);
    if (code == 403) printf("[-] " ID ": %s refused the pre-shared key (set the same key on the TV)\n", host);
    else if (code < 0) printf("[-] " ID ": %s does not answer\n", host);
    else if (code != 200) printf("[-] " ID ": %s answered HTTP %d to %s\n", host, code, path);
    return code == 200 ? 0 : -1;
}

static int send_ircc(const char *host, const char *psk, const char *code) {
    char body[600];
    snprintf(body, sizeof(body),
             "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
             "<u:X_SendIRCC xmlns:u=\"urn:schemas-sony-com:service:IRCC:1\"><IRCCCode>%s</IRCCCode>"
             "</u:X_SendIRCC></s:Body></s:Envelope>", code);
    return post(host, psk, "/sony/IRCC",
                "Content-Type: text/xml; charset=UTF-8\r\n"
                "SOAPACTION: \"urn:schemas-sony-com:service:IRCC:1#X_SendIRCC\"\r\n", body, NULL, 0);
}

/* JSON-RPC call; the answer in resp. 0 when the TV answered without an "error". */
static int rpc(const char *host, const char *psk, const char *service, const char *method, const char *version,
               const char *params, char *resp, size_t resp_sz) {
    char body[700], path[64];
    snprintf(path, sizeof(path), "/sony/%s", service);
    snprintf(body, sizeof(body), "{\"method\":\"%s\",\"id\":1,\"params\":[%s],\"version\":\"%s\"}", method,
             params, version);
    static char local[2048];
    if (!resp) {
        resp = local;
        resp_sz = sizeof(local);
    }
    if (post(host, psk, path, "Content-Type: application/json\r\n", body, resp, resp_sz) != 0) return -1;
    if (strstr(resp, "\"error\"")) {
        printf("[-] " ID ": %s %s: %s\n", service, method, resp);
        return -1;
    }
    return 0;
}

/* "active", "standby", or "" when the TV does not answer */
static void power_status(const char *host, const char *psk, char *out, size_t sz) {
    char resp[512];
    out[0] = '\0';
    if (rpc(host, psk, "system", "getPowerStatus", "1.0", "", resp, sizeof(resp)) == 0) {
        plg_json_str(resp, "status", out, sz);
    }
}

static void do_power(const char *host, const char *psk, const char *cmd, const char *mac) {
    char st[16];
    power_status(host, psk, st, sizeof(st));
    int on = strcmp(st, "active") == 0;
    if (plg_streq(cmd, "PowerToggle")) cmd = on ? "PowerOff" : "PowerOn";
    if (plg_streq(cmd, "PowerOff")) {
        if (on) rpc(host, psk, "system", "setPowerStatus", "1.0", "{\"status\":false}", NULL, 0);
        return;
    }
    if (on) return;
    if (st[0]) { /* standby with the network on ("Remote start"): the TV turns itself on */
        rpc(host, psk, "system", "setPowerStatus", "1.0", "{\"status\":true}", NULL, 0);
        return;
    }
    if (wol_wake(s_data, host, mac, "", 0) != 0) {
        printf("[-] " ID ": %s is off and its MAC is not known: set the MAC Address, or turn the TV on once "
               "while the plugin runs\n", host);
    }
}

/* Held keys the TV should repeat: sent again on each repeat. Others act once. */
static int repeats(const ircc_t *k) {
    static const char *const r[] = {"VolumeUp", "VolumeDown", "ChannelUp", "ChannelDown", "Up", "Down",
                                    "Left", "Right", "DirectionUp", "DirectionDown", "DirectionLeft",
                                    "DirectionRight", "FastForward", "Rewind", "Fwd", "Rev", NULL};
    for (int i = 0; r[i]; i++) if (plg_streq(k->name, r[i])) return 1;
    return 0;
}

static void handle(const char *buf) {
    plg_msg_t m;
    plg_parse(buf, &m);
    if (m.stale || !m.command[0] || !m.host[0]) return;
    char psk[96], mac[32], esc[512], params[600];
    plg_json_str(buf, "psk", psk, sizeof(psk));
    const char *cmd = m.command;

    const ircc_t *k = find_ircc(cmd);
    if (k && !plg_streq(cmd, "PowerToggle") && !plg_streq(cmd, "Power")) {
        if (m.is_release || (m.is_repeat && !repeats(k))) return;
        char code[32];
        ircc_code(k, code, sizeof(code));
        if (send_ircc(m.host, psk, code) == 0) wol_learn(s_data, m.host);
        return;
    }
    if (!plg_once(&m)) return; /* everything below acts once per button */

    if (plg_streq(cmd, "PowerOn") || plg_streq(cmd, "PowerOff") || plg_streq(cmd, "PowerToggle") ||
        plg_streq(cmd, "Power") || plg_streq(cmd, "WakeOnLan")) {
        plg_json_str(buf, "mac", mac, sizeof(mac));
        if (plg_streq(cmd, "WakeOnLan")) wol_wake(s_data, m.host, mac, "", 0);
        else do_power(m.host, psk, plg_streq(cmd, "Power") ? "PowerToggle" : cmd, mac);
    } else if (plg_prefix(cmd, "ircc:")) {
        plg_json_escape(cmd + 5, esc, sizeof(esc));
        send_ircc(m.host, psk, esc);
    } else if (plg_prefix(cmd, "HDMI")) {
        const char *n = cmd + 4;
        while (*n == ' ' || *n == '_') n++;
        snprintf(params, sizeof(params), "{\"uri\":\"extInput:hdmi?port=%c\"}", *n >= '1' && *n <= '9' ? *n : '1');
        rpc(m.host, psk, "avContent", "setPlayContent", "1.0", params, NULL, 0);
    } else if (plg_prefix(cmd, "app:") || plg_prefix(cmd, "launch:")) {
        plg_json_escape(strchr(cmd, ':') + 1, esc, sizeof(esc));
        snprintf(params, sizeof(params), "{\"uri\":\"%s\"}", esc);
        rpc(m.host, psk, "appControl", "setActiveApp", "1.0", params, NULL, 0);
    } else if (plg_prefix(cmd, "text:")) {
        plg_json_escape(cmd + 5, esc, sizeof(esc));
        snprintf(params, sizeof(params), "{\"text\":\"%s\"}", esc);
        rpc(m.host, psk, "appControl", "setTextForm", "1.0", params, NULL, 0);
    } else if (plg_streq(cmd, "Volume")) {
        snprintf(params, sizeof(params), "{\"target\":\"speaker\",\"volume\":\"%ld\"}",
                 m.value < 0 ? 0 : m.value > 100 ? 100 : m.value);
        rpc(m.host, psk, "audio", "setAudioVolume", "1.0", params, NULL, 0);
    } else {
        printf("[-] " ID ": unknown command '%s'\n", cmd);
    }
}

int main(int argc, char **argv) {
    char path[128];
    int fd = plg_open(argc, argv, ID, path, sizeof(path));
    if (fd < 0) return 1;
    s_data = plg_data_dir(ID);
    char buf[4096];
    while (plg_running) {
        int r = plg_recv(fd, buf, sizeof(buf), -1);
        if (r < 0) break;
        if (r > 0) handle(buf);
    }
    close(fd);
    unlink(path);
    printf("[*] " ID ": stopped\n");
    return 0;
}
