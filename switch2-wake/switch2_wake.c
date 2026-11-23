/*
 * Nintendo Switch 2 Bluetooth LE Wake Plugin Daemon for Harmony-NG
 *
 * A sleeping Switch 2 listens on the LE advertising channels for its bonded controllers. It
 * wakes on one non-connectable advertisement that carries Nintendo's manufacturer data, a
 * controller product id, the wake flag 0x81 and the console's own address.
 *
 * - Wake: hub_btstack sends that advertisement from the paired controller's address
 *   ("adv <hex> <ms> addr=<controller>").
 * - Find (pairing without a host): hub_btstack hands this plugin the advertising reports it
 *   hears ("adv_listen"). The paired controller's own wake advertisement, sent when a button is
 *   pressed while the console sleeps, holds all three settings.
 *
 * Protocol research: alexvnesta/switch2controller (see README.md, Attribution). This is an
 * independent implementation; no upstream code is copied.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#define WAKE_ADV_LEN 31
#define BURST_MS 2000
#define DEFAULT_PID 0x2066 /* Joy-Con 2 (L); the console accepts any bonded controller type */
#define FIND_DEFAULT_S 30
#ifndef HUB_BTSTACK_SOCK /* a host test sets its own */
#define HUB_BTSTACK_SOCK "/tmp/hub_btstack.sock"
#endif
#define PLUGIN_ID "switch2-wake"
/* Nintendo's manufacturer data up to the vendor id: the filter for adv_listen */
#define NINTENDO_FILTER "53050100037E05"

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

/* AA:BB:CC:DD:EE:FF, anything else is refused */
static int valid_mac(const char *s) {
    if (strlen(s) != 17) return 0;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (s[i] != ':') return 0;
        } else if (!strchr("0123456789abcdefABCDEF", s[i])) {
            return 0;
        }
    }
    return 1;
}

/* A valid_mac() string to 6 bytes in reading order */
static void parse_mac(const char *s, uint8_t b[6]) {
    for (int i = 0; i < 6; i++) b[i] = (uint8_t)strtoul(s + i * 3, NULL, 16);
}

/* Controller product id: empty gives the default, else 2066, 2067 or 2069 (hex, optional 0x).
 * Returns -1 for anything else. */
static int parse_pid(const char *s) {
    if (!s[0]) return DEFAULT_PID;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (strlen(s) != 4 || strspn(s, "0123456789abcdefABCDEF") != 4) return -1;
    int pid = (int)strtoul(s, NULL, 16);
    return (pid == 0x2066 || pid == 0x2067 || pid == 0x2069) ? pid : -1;
}

/* The 31-byte wake advertisement for the console address sw (reading order) */
static void build_wake_adv(uint8_t out[WAKE_ADV_LEN], const uint8_t sw[6], uint16_t pid) {
    static const uint8_t head[] = {
        0x02, 0x01, 0x06,       /* Flags: LE General Discoverable, BR/EDR not supported */
        0x1B, 0xFF,             /* Manufacturer Specific Data, 27 bytes */
        0x53, 0x05,             /* Company id 0x0553 */
        0x01, 0x00, 0x03,
        0x7E, 0x05,             /* Nintendo vendor id 0x057E */
    };
    memset(out, 0, WAKE_ADV_LEN);
    memcpy(out, head, sizeof(head));
    out[12] = pid & 0xFF;
    out[13] = pid >> 8;
    out[14] = 0x00;
    out[15] = 0x01;
    out[16] = 0x81;             /* Wake (reconnect from sleep) */
    for (int i = 0; i < 6; i++) out[17 + i] = sw[5 - i];
    out[23] = 0x0F;
}

/* A controller's wake advertisement: the header of build_wake_adv() with the wake flag. Gives
 * the console address (reading order) and the product id. 0 for anything else (a pairing
 * advertisement has flag 0x00). */
static int parse_wake_report(const uint8_t *ad, int len, uint8_t sw[6], uint16_t *pid) {
    static const uint8_t head[] = { 0x1B, 0xFF, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7E, 0x05 };
    if (len < 24 || memcmp(ad + 3, head, sizeof(head)) != 0 || ad[16] != 0x81) return 0;
    *pid = (uint16_t)(ad[12] | ad[13] << 8);
    for (int i = 0; i < 6; i++) sw[i] = ad[22 - i];
    return 1;
}

/* Hex (at most 2 * max characters, even) to bytes; -1 for anything else */
static int parse_hex(const char *hex, uint8_t *out, int max) {
    int n = (int)strlen(hex);
    if (n % 2 || n / 2 > max) return -1;
    for (int i = 0; i < n / 2; i++) {
        char b[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        if (strspn(b, "0123456789abcdefABCDEF") != 2) return -1;
        out[i] = (uint8_t)strtoul(b, NULL, 16);
    }
    return n / 2;
}

/* One text command to hub_btstack */
static int btstack_cmd(const char *cmd) {
    int s = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, HUB_BTSTACK_SOCK, sizeof(sun.sun_path) - 1);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ssize_t n = sendto(s, cmd, strlen(cmd), 0, (struct sockaddr *)&sun, sizeof(sun));
    close(s);
    if (n <= 0) {
        printf("[-] switch2-wake: hub_btstack did not take the command: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void send_wake(const uint8_t sw[6], const char *ctrl_mac, uint16_t pid) {
    uint8_t adv[WAKE_ADV_LEN];
    build_wake_adv(adv, sw, pid);
    char cmd[128] = "adv ";
    for (int i = 0; i < WAKE_ADV_LEN; i++) snprintf(cmd + 4 + 2 * i, 3, "%02X", adv[i]);
    snprintf(cmd + 4 + 2 * WAKE_ADV_LEN, sizeof(cmd) - 4 - 2 * WAKE_ADV_LEN, " %d addr=%s\n", BURST_MS, ctrl_mac);
    if (btstack_cmd(cmd) == 0) printf("[+] switch2-wake: wake advertisement queued for %d ms\n", BURST_MS);
}

/* Value of the first "key": in buf, a string read to its closing quote */
static void extract_json_str(const char *buf, const char *key, char *out, size_t out_sz) {
    out[0] = '\0';
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(buf, search);
    if (!p) return;
    p += strlen(search);
    while (*p == ' ') p++;
    int quoted = (*p == '"');
    if (quoted) p++;
    size_t i = 0;
    while (*p && i + 1 < out_sz && (quoted ? *p != '"' : (*p != ',' && *p != '}'))) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

/* The pairing status the Hub reads (/tmp/plugins/switch2-wake.status), written whole */
static char s_status_path[160];

static void write_status(int ok, const char *stage, const char *message, const char *config_json) {
    char tmp[176];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_status_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\"ok\":%s,\"action\":\"start_pair\",\"stage\":\"%s\",\"message\":\"%s\"%s%s}\n",
            ok ? "true" : "false", stage, message, config_json ? ",\"config\":" : "", config_json ? config_json : "");
    fclose(f);
    rename(tmp, s_status_path);
}

static time_t s_find_until = 0; /* a Find runs until then (0: none) */

static long now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

static void find_start(int secs) {
    if (secs <= 0 || secs > 60) secs = FIND_DEFAULT_S;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "adv_listen " PLUGIN_ID " %d " NINTENDO_FILTER "\n", secs);
    if (btstack_cmd(cmd) != 0) {
        write_status(0, "error", "hub_btstack is not running", NULL);
        return;
    }
    s_find_until = now_s() + secs;
    write_status(1, "listening", "Put the Switch 2 to sleep, then press a button on its paired controller", NULL);
    printf("[*] switch2-wake: listening %d s for a controller's wake advertisement\n", secs);
}

static void find_stop(void) {
    s_find_until = 0;
    btstack_cmd("adv_listen " PLUGIN_ID " 0\n");
}

static void on_report(const char *buf) {
    char addr[32], data_hex[80], addr_type[8];
    extract_json_str(buf, "addr", addr, sizeof(addr));
    extract_json_str(buf, "data", data_hex, sizeof(data_hex));
    extract_json_str(buf, "addr_type", addr_type, sizeof(addr_type));
    uint8_t ad[31], sw[6];
    uint16_t pid = 0;
    int len = parse_hex(data_hex, ad, (int)sizeof(ad));
    if (!s_find_until || !valid_mac(addr) || len < 0 || !parse_wake_report(ad, len, sw, &pid)) return;

    char sw_mac[18], cfg[160];
    snprintf(sw_mac, sizeof(sw_mac), "%02X:%02X:%02X:%02X:%02X:%02X", sw[0], sw[1], sw[2], sw[3], sw[4], sw[5]);
    snprintf(cfg, sizeof(cfg), "{\"switch_mac\":\"%s\",\"controller_mac\":\"%s\",\"controller_pid\":\"%04X\"}",
             sw_mac, addr, pid);
    /* The address type tells whether the console may need the controller's public address:
     * the Hub's wake goes out with a random one */
    printf("[+] switch2-wake: found Switch 2 %s, controller %s (pid %04X, address type %s)\n",
           sw_mac, addr, pid, addr_type[0] ? addr_type : "?");
    write_status(1, "done", "Switch 2 found", cfg);
    find_stop();
}

int main(int argc, char **argv) {
    const char *env_sock = getenv("HARMONY_SOCKET");
    const char *sock_path = (env_sock && env_sock[0]) ? env_sock : (argc > 1) ? argv[1] : "/tmp/plugins/" PLUGIN_ID ".sock";

    setlinebuf(stdout);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    /* The status file sits next to the socket (/tmp/plugins) */
    snprintf(s_status_path, sizeof(s_status_path), "%s", sock_path);
    char *dot = strrchr(s_status_path, '.');
    if (dot && strcmp(dot, ".sock") == 0) strcpy(dot, ".status");
    else strncat(s_status_path, ".status", sizeof(s_status_path) - strlen(s_status_path) - 1);

    unlink(sock_path);
    int sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sfd < 0) {
        perror("socket AF_UNIX");
        return 1;
    }
    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, sock_path, sizeof(sun.sun_path) - 1);
    if (bind(sfd, (struct sockaddr *)&sun, sizeof(sun)) != 0) {
        perror("bind AF_UNIX");
        close(sfd);
        return 1;
    }
    /* Wake up every second to end a Find that heard nothing */
    struct timeval rtv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    printf("[*] switch2-wake daemon listening on %s\n", sock_path);

    char buf[4096];
    while (s_running) {
        ssize_t n = recv(sfd, buf, sizeof(buf) - 1, 0);
        if (s_find_until && now_s() >= s_find_until) {
            find_stop();
            write_status(0, "error", "No wake advertisement heard: press a button on the paired controller while the Switch 2 sleeps", NULL);
            printf("[-] switch2-wake: Find ended, nothing heard\n");
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("recv");
            break;
        }
        buf[n] = '\0';

        char command[64], action[32];
        extract_json_str(buf, "command", command, sizeof(command));
        extract_json_str(buf, "action", action, sizeof(action));

        if (strcmp(command, "adv_report") == 0) {
            on_report(buf);
            continue;
        }
        if (strcmp(command, "pair") == 0) {
            if (strcmp(action, "start_pair") == 0) {
                char to[8];
                extract_json_str(buf, "timeout_s", to, sizeof(to));
                find_start(atoi(to));
            }
            continue;
        }

        /* One wake per button: the hub sends press, repeats and release */
        if (strcmp(action, "repeat") == 0 || strcmp(action, "release") == 0) continue;

        if (strcasecmp(command, "Wake") != 0 && strcasecmp(command, "PowerOn") != 0) {
            printf("[-] switch2-wake: command '%s' ignored (only Wake and PowerOn)\n", command);
            continue;
        }

        char sw_mac[32], ctrl_mac[32], pid_str[16];
        extract_json_str(buf, "switch_mac", sw_mac, sizeof(sw_mac));
        extract_json_str(buf, "controller_mac", ctrl_mac, sizeof(ctrl_mac));
        extract_json_str(buf, "controller_pid", pid_str, sizeof(pid_str));

        if (!valid_mac(sw_mac)) {
            printf("[-] switch2-wake: switch_mac missing or not AA:BB:CC:DD:EE:FF, wake skipped\n");
            continue;
        }
        if (!valid_mac(ctrl_mac)) {
            printf("[-] switch2-wake: controller_mac missing or not AA:BB:CC:DD:EE:FF, wake skipped\n");
            continue;
        }
        int pid = parse_pid(pid_str);
        if (pid < 0) {
            printf("[-] switch2-wake: controller_pid '%s' not 2066, 2067 or 2069, wake skipped\n", pid_str);
            continue;
        }

        uint8_t sw[6];
        parse_mac(sw_mac, sw);
        printf("[*] switch2-wake: waking %s as controller %s (pid %04x)\n", sw_mac, ctrl_mac, pid);
        send_wake(sw, ctrl_mac, (uint16_t)pid);
    }

    if (s_find_until) find_stop();
    close(sfd);
    unlink(sock_path);
    printf("[*] switch2-wake daemon stopped\n");
    return 0;
}
