#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>

struct sockaddr_hci {
    unsigned short hci_family;
    unsigned short hci_dev;
    unsigned short hci_channel;
};

struct hci_filter {
    unsigned long type_mask;
    unsigned long event_mask[2];
    unsigned short opcode;
};

#define HCI_COMMAND_PKT 0x01
#define HCI_EVENT_PKT   0x04

#define HCI_OP_CREATE_CONN 0x0405
#define HCI_EV_CONN_COMPLETE 0x03
#define HCI_EV_CMD_STATUS 0x0F
#define HCI_EV_DISCONN_COMPLETE 0x05

#define SOL_HCI 0
#define HCI_FILTER 2

static int parse_mac(const char *str, unsigned char *b) {
    unsigned int m[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) return -1;
    for (int i = 0; i < 6; i++) b[i] = (unsigned char)m[i];
    return 0;
}

static const char *hci_err_str(unsigned char status) {
    switch (status) {
        case 0x00: return "Success";
        case 0x04: return "Page Timeout (PS5 did not answer page)";
        case 0x0c: return "Command Disallowed";
        case 0x0f: return "Connection Rejected due to Unacceptable BD_ADDR";
        case 0x10: return "Connection Accept Timeout";
        case 0x13: return "Remote User Terminated Connection (PS5 woke and reset link)";
        case 0x16: return "Connection Terminated by Local Host";
        default: return "Other error";
    }
}

int main(int argc, char **argv) {
    const char *target_str = (argc > 1) ? argv[1] : "84:E6:57:C2:0D:97";
    int page_mode = (argc > 2) ? atoi(argv[2]) : 2; // Default R2 (0x02)
    int timeout_sec = (argc > 3) ? atoi(argv[3]) : 15; // Default 15s

    unsigned char target_mac[6];
    if (parse_mac(target_str, target_mac) < 0) {
        fprintf(stderr, "Invalid MAC: %s\n", target_str);
        return 1;
    }

    printf("[+] Opening raw HCI socket for hci0...\n");
    int s = socket(31 /* AF_BLUETOOTH */, SOCK_RAW, 1 /* BTPROTO_HCI */);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    struct hci_filter flt;
    memset(&flt, 0, sizeof(flt));
    flt.type_mask = (1 << HCI_EVENT_PKT);
    flt.event_mask[0] = 0xffffffff;
    flt.event_mask[1] = 0xffffffff;
    setsockopt(s, SOL_HCI, HCI_FILTER, &flt, sizeof(flt));

    struct sockaddr_hci addr;
    memset(&addr, 0, sizeof(addr));
    addr.hci_family = 31;
    addr.hci_dev = 0;
    addr.hci_channel = 0;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return 1;
    }

    unsigned char cmd[17];
    cmd[0] = HCI_COMMAND_PKT;
    cmd[1] = 0x05; cmd[2] = 0x04; // Opcode 0x0405 (HCI_Create_Connection)
    cmd[3] = 13;                  // Param len
    // Target BD_ADDR in little-endian
    cmd[4] = target_mac[5];
    cmd[5] = target_mac[4];
    cmd[6] = target_mac[3];
    cmd[7] = target_mac[2];
    cmd[8] = target_mac[1];
    cmd[9] = target_mac[0];
    cmd[10] = 0x18; cmd[11] = 0xCC; // Packet type 0xCC18
    cmd[12] = (unsigned char)page_mode; // Page scan repetition mode R2 (0x02)
    cmd[13] = 0x00;                 // Reserved
    cmd[14] = 0x00; cmd[15] = 0x00; // Clock offset 0x0000
    cmd[16] = 0x01;                 // Allow role switch (0x01)

    printf("[+] Paging target %s (psrm=0x%02x, role_switch=1, ptype=0xCC18, timeout=%ds)...\n",
           target_str, page_mode, timeout_sec);

    if (write(s, cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("write HCI command");
        close(s);
        return 1;
    }

    struct timeval start, now;
    gettimeofday(&start, NULL);
    int result = -1;

    while (1) {
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
        long remain_ms = (timeout_sec * 1000) - elapsed_ms;
        if (remain_ms <= 0) {
            printf("[-] Timeout reached (%ds) with no Connection Complete.\n", timeout_sec);
            result = 0x04;
            break;
        }

        struct pollfd pfd;
        pfd.fd = s;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pret = poll(&pfd, 1, remain_ms);
        if (pret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (pret == 0) {
            printf("[-] Poll timeout reached (%ds).\n", timeout_sec);
            result = 0x04;
            break;
        }

        unsigned char buf[256];
        int n = read(s, buf, sizeof(buf));
        if (n <= 0) break;

        // Skip leading packet type byte 0x04 if present
        int offset = 0;
        if (buf[0] == HCI_EVENT_PKT && n > 2) offset = 1;

        unsigned char ev = buf[offset];
        unsigned char plen = buf[offset + 1];
        unsigned char *pdata = &buf[offset + 2];

        if (ev == HCI_EV_CMD_STATUS && plen >= 4) {
            unsigned char status = pdata[0];
            unsigned short opcode = pdata[2] | (pdata[3] << 8);
            if (opcode == HCI_OP_CREATE_CONN) {
                printf("[*] HCI_Create_Connection status: 0x%02x (%s)\n", status, status ? "REJECTED" : "PENDING");
                if (status != 0) {
                    result = status;
                    break;
                }
            }
        } else if (ev == HCI_EV_CONN_COMPLETE && plen >= 11) {
            unsigned char status = pdata[0];
            unsigned short handle = pdata[1] | (pdata[2] << 8);
            printf("[*] HCI Connection Complete: status=0x%02x (%s) handle=0x%04x\n",
                   status, hci_err_str(status), handle);
            if (status == 0x00 || status == 0x13) {
                printf("[+] SUCCESS! PS5 acknowledged connection! Console should be waking up!\n");
                result = 0;
            } else {
                result = status;
            }
            break;
        } else if (ev == HCI_EV_DISCONN_COMPLETE) {
            printf("[*] HCI Disconnection Complete\n");
            result = 0;
            break;
        }
    }

    close(s);
    return result;
}
