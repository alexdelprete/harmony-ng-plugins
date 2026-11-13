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
#include <fcntl.h>
#include <errno.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_HCI
#define BTPROTO_HCI 1
#endif

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

static int parse_mac_bytes(const char *str, unsigned char *out_le) {
    unsigned int m[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
        return -1;
    }
    /* Little endian for HCI packets (byte 5 down to 0) */
    out_le[0] = (unsigned char)m[5];
    out_le[1] = (unsigned char)m[4];
    out_le[2] = (unsigned char)m[3];
    out_le[3] = (unsigned char)m[2];
    out_le[4] = (unsigned char)m[1];
    out_le[5] = (unsigned char)m[0];
    return 0;
}

static void trigger_ps5_hci_wake(const char *host_mac, const char *ctrl_mac) {
    unsigned char target_le[6];
    if (parse_mac_bytes(host_mac, target_le) != 0) return;

    int s = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (s >= 0) {
        struct sockaddr {
            unsigned short sa_family;
            char sa_data[14];
        } sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_family = AF_BLUETOOTH;

        /* If controller mac provided, issue Atheros AR3012 0xFC0B BD_ADDR write */
        if (ctrl_mac && ctrl_mac[0]) {
            unsigned char ctrl_le[6];
            if (parse_mac_bytes(ctrl_mac, ctrl_le) == 0) {
                unsigned char ath_pkt[14];
                ath_pkt[0] = 0x01; /* HCI_CMD */
                ath_pkt[1] = 0x0B; ath_pkt[2] = 0xFC; /* Opcode 0xFC0B */
                ath_pkt[3] = 10;   /* Param length */
                ath_pkt[4] = 0x01; ath_pkt[5] = 0x01; ath_pkt[6] = 0x00; ath_pkt[7] = 0x06;
                memcpy(&ath_pkt[8], ctrl_le, 6);
                send(s, ath_pkt, sizeof(ath_pkt), 0);
                usleep(50000);
            }
        }

        /* 17-byte HCI Create Connection (0x0405) packet */
        unsigned char pkt[17];
        pkt[0] = 0x01;             /* HCI_CMD indicator */
        pkt[1] = 0x05; pkt[2] = 0x04; /* Opcode 0x0405 */
        pkt[3] = 13;               /* Parameter length */
        memcpy(&pkt[4], target_le, 6);
        pkt[10] = 0x18; pkt[11] = 0xCC; /* Packet type 0xCC18 (DM1/DH1/DM3/DH3/DM5/DH5) */
        pkt[12] = 0x02;            /* Page scan repetition mode R2 */
        pkt[13] = 0x00;            /* Reserved */
        pkt[14] = 0x00; pkt[15] = 0x00; /* Clock offset */
        pkt[16] = 0x01;            /* Allow role switch */

        send(s, pkt, sizeof(pkt), 0);
        close(s);
        printf("[*] ps5-wake: Raw HCI connection packet dispatched to %s\n", host_mac);
    }
}

static void trigger_ps5_btstack_wake(const char *host_mac) {
    int fd = open("/tmp/bthid_input", O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[128];
        int len = snprintf(cmd, sizeof(cmd), "TARGET %s\nconnect_host %s\n", host_mac, host_mac);
        if (len > 0) write(fd, cmd, (size_t)len);
        close(fd);
        printf("[*] ps5-wake: BTstack connect dispatched for %s\n", host_mac);
    }
}

static void trigger_ps5_udp_wake(const char *host_ip, const char *host_mac) {
    if (!host_ip || !host_ip[0]) return;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;

    int broadcast = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(9387);
    inet_pton(AF_INET, host_ip, &sin.sin_addr);

    char pkt[256];
    int len = snprintf(pkt, sizeof(pkt), "AWAKE %s\r\n", host_mac ? host_mac : "");
    if (len > 0) {
        sendto(s, pkt, (size_t)len, 0, (struct sockaddr *)&sin, sizeof(sin));
        /* Also send to port 9295 */
        sin.sin_port = htons(9295);
        sendto(s, pkt, (size_t)len, 0, (struct sockaddr *)&sin, sizeof(sin));
    }
    close(s);
    printf("[*] ps5-wake: UDP rest-mode wake datagram sent to %s:9387/9295\n", host_ip);
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

        char host_ip[64] = {0};
        extract_json_str(buf, "host_ip", host_ip, sizeof(host_ip));

        if (host_mac[0]) {
            printf("[*] ps5-wake: Triggering PlayStation wake sequence for %s\n", host_mac);
            /* Multi-tier wake */
            trigger_ps5_hci_wake(host_mac, ctrl_mac);
            trigger_ps5_btstack_wake(host_mac);
            if (host_ip[0]) trigger_ps5_udp_wake(host_ip, host_mac);
        }
    }

    close(sfd);
    unlink(sock_path);
    printf("[*] ps5-wake daemon stopped\n");
    return 0;
}
