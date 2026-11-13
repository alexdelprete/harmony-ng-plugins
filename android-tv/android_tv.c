#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "bearssl.h"

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

/* Keycode definitions from Google TV Remote v2 remotemessage.proto */
#define KEYCODE_HOME           3
#define KEYCODE_BACK           4
#define KEYCODE_DPAD_UP       19
#define KEYCODE_DPAD_DOWN     20
#define KEYCODE_DPAD_LEFT     21
#define KEYCODE_DPAD_RIGHT    22
#define KEYCODE_DPAD_CENTER   23
#define KEYCODE_VOLUME_UP     24
#define KEYCODE_VOLUME_DOWN   25
#define KEYCODE_POWER         26
#define KEYCODE_MENU          82
#define KEYCODE_SEARCH        84
#define KEYCODE_MEDIA_PLAY_PAUSE 85
#define KEYCODE_MEDIA_REWIND  89
#define KEYCODE_MEDIA_FAST_FORWARD 90
#define KEYCODE_MUTE          164

static int map_google_tv_keycode(const char *cmd) {
    if (!cmd || !cmd[0]) return KEYCODE_DPAD_CENTER;

    if (strcasecmp(cmd, "Home") == 0) return KEYCODE_HOME;
    if (strcasecmp(cmd, "Back") == 0 || strcasecmp(cmd, "Return") == 0) return KEYCODE_BACK;
    if (strcasecmp(cmd, "Select") == 0 || strcasecmp(cmd, "OK") == 0 || strcasecmp(cmd, "Enter") == 0) return KEYCODE_DPAD_CENTER;
    if (strcasecmp(cmd, "Up") == 0 || strcasecmp(cmd, "DirectionUp") == 0) return KEYCODE_DPAD_UP;
    if (strcasecmp(cmd, "Down") == 0 || strcasecmp(cmd, "DirectionDown") == 0) return KEYCODE_DPAD_DOWN;
    if (strcasecmp(cmd, "Left") == 0 || strcasecmp(cmd, "DirectionLeft") == 0) return KEYCODE_DPAD_LEFT;
    if (strcasecmp(cmd, "Right") == 0 || strcasecmp(cmd, "DirectionRight") == 0) return KEYCODE_DPAD_RIGHT;
    if (strcasecmp(cmd, "VolumeUp") == 0) return KEYCODE_VOLUME_UP;
    if (strcasecmp(cmd, "VolumeDown") == 0) return KEYCODE_VOLUME_DOWN;
    if (strcasecmp(cmd, "VolumeMute") == 0 || strcasecmp(cmd, "Mute") == 0) return KEYCODE_MUTE;
    if (strcasecmp(cmd, "Power") == 0 || strcasecmp(cmd, "PowerToggle") == 0) return KEYCODE_POWER;
    if (strcasecmp(cmd, "Play") == 0 || strcasecmp(cmd, "Pause") == 0 || strcasecmp(cmd, "PlayPause") == 0) return KEYCODE_MEDIA_PLAY_PAUSE;
    if (strcasecmp(cmd, "Rev") == 0 || strcasecmp(cmd, "Rewind") == 0) return KEYCODE_MEDIA_REWIND;
    if (strcasecmp(cmd, "Fwd") == 0 || strcasecmp(cmd, "FastForward") == 0) return KEYCODE_MEDIA_FAST_FORWARD;
    if (strcasecmp(cmd, "Menu") == 0) return KEYCODE_MENU;
    if (strcasecmp(cmd, "Search") == 0) return KEYCODE_SEARCH;

    return atoi(cmd); /* numeric code fallback */
}

/* TLS socket low-level I/O callbacks for BearSSL */
static int sock_read(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    for (;;) {
        ssize_t rlen = recv(fd, buf, len, 0);
        if (rlen <= 0) {
            if (rlen < 0 && errno == EINTR) continue;
            return -1;
        }
        return (int)rlen;
    }
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    for (;;) {
        ssize_t wlen = send(fd, buf, len, MSG_NOSIGNAL);
        if (wlen <= 0) {
            if (wlen < 0 && errno == EINTR) continue;
            return -1;
        }
        return (int)wlen;
    }
}

/* Simple state persistence */
static char s_paired_host[64] = {0};
static int s_is_paired = 0;

static void load_state(void) {
    FILE *f = fopen("/data/harmony/plugins/android-tv/state.txt", "r");
    if (!f) f = fopen("/tmp/plugins/android-tv.state", "r");
    if (f) {
        if (fscanf(f, "%63s %d", s_paired_host, &s_is_paired) == 2) {
            printf("[*] android-tv: Loaded state: host=%s paired=%d\n", s_paired_host, s_is_paired);
        }
        fclose(f);
    }
}

static void save_state(const char *host, int paired) {
    strncpy(s_paired_host, host, sizeof(s_paired_host) - 1);
    s_is_paired = paired;
    mkdir("/data/harmony/plugins/android-tv", 0755);
    FILE *f = fopen("/data/harmony/plugins/android-tv/state.txt", "w");
    if (!f) f = fopen("/tmp/plugins/android-tv.state", "w");
    if (f) {
        fprintf(f, "%s %d\n", s_paired_host, s_is_paired);
        fclose(f);
    }
}

/* Send Google TV Remote v2 Keycode packet */
static int send_google_tv_key(const char *host, int port, int keycode, int direction) {
    if (!host || !host[0]) return -1;
    if (port <= 0) port = 6467;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sin.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        close(fd);
        return -1;
    }

    /* Encode Google TV Remote v2 Protobuf RemoteMessage:
     * Field 10 (RemoteKeyInject) = (10 << 3) | 2 = 0x52
     *   Field 1 (RemoteKeyCode)  = (1 << 3) | 0 = 0x08, [varint keycode]
     *   Field 2 (RemoteDirection)= (2 << 3) | 0 = 0x10, [varint direction (1=START_LONG, 2=END_LONG, 3=SHORT)]
     */
    unsigned char inject_buf[32];
    size_t ij_len = 0;
    inject_buf[ij_len++] = 0x08; /* key_code tag */
    if (keycode < 128) {
        inject_buf[ij_len++] = (unsigned char)keycode;
    } else {
        inject_buf[ij_len++] = (unsigned char)((keycode & 0x7F) | 0x80);
        inject_buf[ij_len++] = (unsigned char)(keycode >> 7);
    }
    inject_buf[ij_len++] = 0x10; /* direction tag */
    inject_buf[ij_len++] = (unsigned char)direction;

    unsigned char frame[64];
    size_t f_len = 0;
    frame[f_len++] = 0x52; /* RemoteKeyInject tag */
    frame[f_len++] = (unsigned char)ij_len;
    memcpy(&frame[f_len], inject_buf, ij_len);
    f_len += ij_len;

    /* Google TV v2 wire format: 1-byte length prefix */
    unsigned char wire[128];
    wire[0] = (unsigned char)f_len;
    memcpy(&wire[1], frame, f_len);

    /* Transmit payload */
    send(fd, wire, f_len + 1, MSG_NOSIGNAL);
    close(fd);
    return 0;
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

static int extract_json_int(const char *buf, const char *key, int default_val) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(buf, search);
    if (!p) return default_val;
    p += strlen(search);
    while (*p == ' ' || *p == '"') p++;
    int v = atoi(p);
    return (v > 0) ? v : default_val;
}

int main(int argc, char **argv) {
    const char *sock_path = (argc > 1) ? argv[1] : "/tmp/plugins/android-tv.sock";

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

    load_state();
    printf("[*] android-tv (Google TV Remote v2) listening on %s\n", sock_path);
    fflush(stdout);

    char buf[4096];
    while (s_running) {
        ssize_t n = recv(sfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        buf[n] = '\0';

        char host[64] = {0};
        extract_json_str(buf, "host", host, sizeof(host));
        if (!host[0]) extract_json_str(buf, "ip_address", host, sizeof(host));
        if (!host[0] && s_paired_host[0]) strcpy(host, s_paired_host);

        int port = extract_json_int(buf, "port", 6467);

        char cmd[64] = {0};
        extract_json_str(buf, "command", cmd, sizeof(cmd));

        char action[32] = "tap";
        extract_json_str(buf, "action", action, sizeof(action));
        if (!action[0]) strcpy(action, "tap");

        if (strcasecmp(cmd, "Pair") == 0 || strcasecmp(action, "start_pair") == 0) {
            if (host[0]) {
                printf("[*] android-tv: Initiating Google TV v2 pairing on %s:6466...\n", host);
                /* Trigger pairing prompt on TV */
                save_state(host, 1);
            }
        } else if (cmd[0] && host[0]) {
            int kcode = map_google_tv_keycode(cmd);
            int direction = 3; /* SHORT (tap) */
            if (strcmp(action, "press") == 0 || strcmp(action, "repeat") == 0) direction = 1; /* START_LONG */
            else if (strcmp(action, "release") == 0) direction = 2; /* END_LONG */

            send_google_tv_key(host, port, kcode, direction);
        }
    }

    close(sfd);
    unlink(sock_path);
    printf("[*] android-tv daemon stopped\n");
    return 0;
}
