/*
 * PlayStation 5 / PlayStation 4 Bluetooth Wake Plugin Daemon for Harmony-NG
 *
 * Supports:
 * - Cold wake from complete power-off via DualSense MAC spoofing helper
 * - Rest-mode wake via generic bt_page request to hub_btstack IPC
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define HUB_BTSTACK_SOCK "/tmp/hub_btstack.sock"

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

/* AA:BB:CC:DD:EE:FF. The addresses go into a shell command line: anything else is refused. */
static int valid_mac(const char *s) {
    if (strlen(s) != 17) return 0;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (s[i] != ':') return 0;
        } else if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static void trigger_ps5_btstack_wake(const char *host_mac, const char *ctrl_mac) {
    if (ctrl_mac[0]) {
        char cmd[512];
        const char *helper = "./bin/ps5_wake_helper.sh";
        if (access(helper, X_OK) != 0) {
            helper = "/data/harmony/plugins/ps5-wake/bin/ps5_wake_helper.sh";
        }
        /* The helper's output goes to this daemon's log; it refuses to run while another
         * radio task (an earlier wake) holds the radio */
        snprintf(cmd, sizeof(cmd), "%s '%s' '%s' 2>&1 &", helper, host_mac, ctrl_mac);
        printf("[*] ps5-wake: starting the cold-wake helper\n");
        fflush(stdout);
        system(cmd);
        return;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "bt_page %s\n", host_mac);

    int s = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s >= 0) {
        struct sockaddr_un sun;
        memset(&sun, 0, sizeof(sun));
        sun.sun_family = AF_UNIX;
        strncpy(sun.sun_path, HUB_BTSTACK_SOCK, sizeof(sun.sun_path) - 1);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ssize_t sent = sendto(s, cmd, strlen(cmd), 0, (struct sockaddr *)&sun, sizeof(sun));
        close(s);
        if (sent > 0) {
            printf("[*] ps5-wake: Dispatched page request to hub_btstack IPC\n");
        } else {
            fprintf(stderr, "[!] ps5-wake: Failed to send to %s: %s\n", HUB_BTSTACK_SOCK, strerror(errno));
        }
        fflush(stdout);
    }
}

/* Value of the first "key": in buf, a string read to its closing quote (escapes are not
 * undone: a MAC address holds none) */
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

int main(int argc, char **argv) {
    const char *sock_path = (argc > 1) ? argv[1] : "/tmp/plugins/ps5-wake.sock";

    setlinebuf(stdout);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

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

    printf("[*] ps5-wake daemon listening on %s\n", sock_path);
    fflush(stdout);

    char buf[4096];
    while (s_running) {
        ssize_t n = recv(sfd, buf, sizeof(buf) - 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
        buf[n] = '\0';  /* an empty datagram is ignored: it used to stop the daemon */

        char command[64], action[32];
        extract_json_str(buf, "command", command, sizeof(command));
        extract_json_str(buf, "action", action, sizeof(action));

        /* One wake per button: the hub sends press, repeats and release, and each one
         * started a wake of its own */
        if (strcmp(action, "repeat") == 0 || strcmp(action, "release") == 0) continue;

        if (strcasecmp(command, "Wake") != 0 && strcasecmp(command, "PowerOn") != 0) {
            printf("[-] ps5-wake: command '%s' ignored (only Wake and PowerOn)\n", command);
            continue;
        }

        char host_mac[32], ctrl_mac[32];
        extract_json_str(buf, "host_mac", host_mac, sizeof(host_mac));
        if (!host_mac[0]) extract_json_str(buf, "mac", host_mac, sizeof(host_mac));
        extract_json_str(buf, "controller_mac", ctrl_mac, sizeof(ctrl_mac));

        if (!valid_mac(host_mac)) {
            printf("[-] ps5-wake: host_mac missing or not AA:BB:CC:DD:EE:FF, wake skipped\n");
            continue;
        }
        if (ctrl_mac[0] && !valid_mac(ctrl_mac)) {
            printf("[-] ps5-wake: controller_mac not AA:BB:CC:DD:EE:FF, wake skipped\n");
            continue;
        }

        printf("[*] ps5-wake: wake requested (%s)\n", ctrl_mac[0] ? "cold wake as the controller" : "rest-mode page");
        fflush(stdout);
        trigger_ps5_btstack_wake(host_mac, ctrl_mac);
    }

    close(sfd);
    unlink(sock_path);
    printf("[*] ps5-wake daemon stopped\n");
    return 0;
}
