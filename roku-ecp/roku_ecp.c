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
#include <ctype.h>

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

static const char *map_roku_key(const char *cmd) {
    if (!cmd || !cmd[0]) return "Select";

    if (strcasecmp(cmd, "Home") == 0) return "Home";
    if (strcasecmp(cmd, "Back") == 0 || strcasecmp(cmd, "Return") == 0) return "Back";
    if (strcasecmp(cmd, "Select") == 0 || strcasecmp(cmd, "OK") == 0 || strcasecmp(cmd, "Enter") == 0) return "Select";
    if (strcasecmp(cmd, "Up") == 0 || strcasecmp(cmd, "DirectionUp") == 0) return "Up";
    if (strcasecmp(cmd, "Down") == 0 || strcasecmp(cmd, "DirectionDown") == 0) return "Down";
    if (strcasecmp(cmd, "Left") == 0 || strcasecmp(cmd, "DirectionLeft") == 0) return "Left";
    if (strcasecmp(cmd, "Right") == 0 || strcasecmp(cmd, "DirectionRight") == 0) return "Right";
    if (strcasecmp(cmd, "Play") == 0 || strcasecmp(cmd, "Pause") == 0 || strcasecmp(cmd, "PlayPause") == 0) return "Play";
    if (strcasecmp(cmd, "Rev") == 0 || strcasecmp(cmd, "Rewind") == 0) return "Rev";
    if (strcasecmp(cmd, "Fwd") == 0 || strcasecmp(cmd, "FastForward") == 0) return "Fwd";
    if (strcasecmp(cmd, "VolumeUp") == 0) return "VolumeUp";
    if (strcasecmp(cmd, "VolumeDown") == 0) return "VolumeDown";
    if (strcasecmp(cmd, "VolumeMute") == 0 || strcasecmp(cmd, "Mute") == 0) return "VolumeMute";
    if (strcasecmp(cmd, "PowerOff") == 0) return "PowerOff";
    if (strcasecmp(cmd, "PowerOn") == 0) return "PowerOn";
    if (strcasecmp(cmd, "Power") == 0 || strcasecmp(cmd, "PowerToggle") == 0) return "Power";
    if (strcasecmp(cmd, "InstantReplay") == 0 || strcasecmp(cmd, "Replay") == 0) return "InstantReplay";
    if (strcasecmp(cmd, "Info") == 0 || strcasecmp(cmd, "Guide") == 0) return "Info";
    if (strcasecmp(cmd, "Search") == 0) return "Search";
    if (strcasecmp(cmd, "Backspace") == 0) return "Backspace";
    if (strcasecmp(cmd, "ChannelUp") == 0) return "ChannelUp";
    if (strcasecmp(cmd, "ChannelDown") == 0) return "ChannelDown";
    if (strcasecmp(cmd, "InputHDMI1") == 0) return "InputHDMI1";
    if (strcasecmp(cmd, "InputHDMI2") == 0) return "InputHDMI2";
    if (strcasecmp(cmd, "InputHDMI3") == 0) return "InputHDMI3";
    if (strcasecmp(cmd, "InputHDMI4") == 0) return "InputHDMI4";
    if (strcasecmp(cmd, "InputAV1") == 0) return "InputAV1";
    if (strcasecmp(cmd, "InputTuner") == 0) return "InputTuner";

    return cmd; /* Custom pass-through key */
}

static int send_roku_http_post(const char *ip, int port, const char *path) {
    if (!ip || !ip[0] || !path || !path[0]) return -1;
    if (port <= 0) port = 8060;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &sin.sin_addr) <= 0) {
        close(s);
        return -1;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 800000 }; /* 800ms timeout */
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        close(s);
        return -1;
    }

    char req[512];
    int len = snprintf(req, sizeof(req),
                       "POST /%s HTTP/1.1\r\n"
                       "Host: %s:%d\r\n"
                       "User-Agent: Harmony-Hub-Plugin\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n\r\n",
                       path, ip, port);
    if (len > 0 && (size_t)len < sizeof(req)) {
        send(s, req, (size_t)len, MSG_NOSIGNAL);
    }

    close(s);
    return 0;
}

static void send_roku_ecp(const char *ip, int port, const char *endpoint, const char *key) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", endpoint, key);
    send_roku_http_post(ip, port, path);
}

static void send_roku_text(const char *ip, int port, const char *text) {
    if (!text) return;
    for (const char *p = text; *p; p++) {
        char lit[64];
        if (isalnum((unsigned char)*p)) {
            snprintf(lit, sizeof(lit), "keypress/Lit_%c", *p);
        } else {
            snprintf(lit, sizeof(lit), "keypress/Lit_%%%02X", (unsigned char)*p);
        }
        send_roku_http_post(ip, port, lit);
        usleep(30000);
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
    const char *sock_path = (argc > 1) ? argv[1] : "/tmp/plugins/roku-ecp.sock";

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

    printf("[*] roku-ecp daemon listening on %s\n", sock_path);

    char buf[4096];
    while (s_running) {
        ssize_t n = recv(sfd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        buf[n] = '\0';

        char ip[64] = {0};
        extract_json_str(buf, "ip_address", ip, sizeof(ip));
        if (!ip[0]) extract_json_str(buf, "host", ip, sizeof(ip));

        int port = extract_json_int(buf, "port", 8060);

        char cmd[64] = {0};
        extract_json_str(buf, "command", cmd, sizeof(cmd));

        char action[32] = "tap";
        extract_json_str(buf, "action", action, sizeof(action));
        if (!action[0]) strcpy(action, "tap");

        if (cmd[0] && ip[0]) {
            printf("[*] roku-ecp: Executing command '%s' (action: %s) on %s:%d\n", cmd, action, ip, port);
            if (strncasecmp(cmd, "Launch:", 7) == 0 || strncasecmp(cmd, "App:", 4) == 0) {
                const char *app_id = strchr(cmd, ':') + 1;
                char path[128];
                snprintf(path, sizeof(path), "launch/%s", app_id);
                send_roku_http_post(ip, port, path);
            } else if (strncasecmp(cmd, "Text:", 5) == 0) {
                const char *text = cmd + 5;
                send_roku_text(ip, port, text);
            } else {
                const char *endpoint = "keypress";
                if (strcmp(action, "press") == 0 || strcmp(action, "repeat") == 0) endpoint = "keydown";
                else if (strcmp(action, "release") == 0) endpoint = "keyup";

                const char *mapped = map_roku_key(cmd);
                send_roku_ecp(ip, port, endpoint, mapped);
            }
        }
    }

    close(sfd);
    unlink(sock_path);
    printf("[*] roku-ecp daemon stopped\n");
    return 0;
}
