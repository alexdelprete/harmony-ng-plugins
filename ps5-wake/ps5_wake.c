/*
 * PlayStation 5 / PlayStation 4 Bluetooth Wake Plugin Daemon for Harmony-NG
 *
 * Delegates Bluetooth connection requests directly to hub_btstack via IPC datagram socket.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
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

static void trigger_ps5_btstack_wake(const char *host_mac, const char *ctrl_mac) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ps5_wake %s %s\n", host_mac, (ctrl_mac && ctrl_mac[0]) ? ctrl_mac : "");

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
            printf("[*] ps5-wake: Dispatched connection request to hub_btstack IPC: %s", cmd);
        } else {
            fprintf(stderr, "[!] ps5-wake: Failed to send to %s: %s\n", HUB_BTSTACK_SOCK, strerror(errno));
        }
    }
}

static void extract_json_str(const char *buf, const char *key, char *out, size_t out_sz) {
    out[0] = '\0';
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(buf, search);
    if (!p) return;
    p += strlen(search);
    while (*p == ' ' || *p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && *p != '\n' && *p != '\r' && i + 1 < out_sz) {
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
        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        buf[n] = '\0';

        char host_mac[64] = {0};
        extract_json_str(buf, "host_mac", host_mac, sizeof(host_mac));
        if (!host_mac[0]) extract_json_str(buf, "mac", host_mac, sizeof(host_mac));

        char ctrl_mac[64] = {0};
        extract_json_str(buf, "controller_mac", ctrl_mac, sizeof(ctrl_mac));

        if (host_mac[0]) {
            printf("[*] ps5-wake: Received wake request for %s (ctrl: %s)\n",
                   host_mac, ctrl_mac[0] ? ctrl_mac : "none");
            trigger_ps5_btstack_wake(host_mac, ctrl_mac);
        }
    }

    close(sfd);
    unlink(sock_path);
    printf("[*] ps5-wake daemon stopped\n");
    return 0;
}
