#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "bearssl.h"
#include "atv_cert_data.h"

#define ATV_PORT_CONTROL 6466
#define ATV_PORT_PAIR    6467

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

/* =========================================================================
 * Google TV Remote v2 (Android TV) Keycodes
 * Matching Home Assistant components/androidtv_remote and TvKeys
 * ========================================================================= */
#define KEYCODE_UNKNOWN              0
#define KEYCODE_HOME                 3
#define KEYCODE_BACK                 4
#define KEYCODE_0                    7
#define KEYCODE_1                    8
#define KEYCODE_2                    9
#define KEYCODE_3                   10
#define KEYCODE_4                   11
#define KEYCODE_5                   12
#define KEYCODE_6                   13
#define KEYCODE_7                   14
#define KEYCODE_8                   15
#define KEYCODE_9                   16
#define KEYCODE_DPAD_UP             19
#define KEYCODE_DPAD_DOWN           20
#define KEYCODE_DPAD_LEFT           21
#define KEYCODE_DPAD_RIGHT          22
#define KEYCODE_DPAD_CENTER         23
#define KEYCODE_VOLUME_UP           24
#define KEYCODE_VOLUME_DOWN         25
#define KEYCODE_POWER               26
#define KEYCODE_ENTER               66
#define KEYCODE_DEL                 67
#define KEYCODE_MENU                82
#define KEYCODE_NOTIFICATION       83
#define KEYCODE_SEARCH              84
#define KEYCODE_MEDIA_PLAY_PAUSE    85
#define KEYCODE_MEDIA_STOP          86
#define KEYCODE_MEDIA_NEXT          87
#define KEYCODE_MEDIA_PREVIOUS      88
#define KEYCODE_MEDIA_REWIND        89
#define KEYCODE_MEDIA_FAST_FORWARD  90
#define KEYCODE_MUTE                91
#define KEYCODE_CHANNEL_UP         166
#define KEYCODE_CHANNEL_DOWN       167
#define KEYCODE_MEDIA_PLAY         126
#define KEYCODE_MEDIA_PAUSE        127
#define KEYCODE_MEDIA_RECORD       130
#define KEYCODE_INFO               165
#define KEYCODE_TV                 170
#define KEYCODE_GUIDE              172
#define KEYCODE_DVR                173
#define KEYCODE_CAPTIONS           175
#define KEYCODE_SETTINGS           176
#define KEYCODE_TV_POWER           177
#define KEYCODE_TV_INPUT           178
#define KEYCODE_PROG_RED           183
#define KEYCODE_PROG_GREEN         184
#define KEYCODE_PROG_YELLOW        185
#define KEYCODE_PROG_BLUE          186
#define KEYCODE_APP_SWITCH         187
#define KEYCODE_PAIRING            225
#define KEYCODE_LAST_CHANNEL       229
#define KEYCODE_TV_INPUT_HDMI_1    243
#define KEYCODE_TV_INPUT_HDMI_2    244
#define KEYCODE_TV_INPUT_HDMI_3    245
#define KEYCODE_TV_INPUT_HDMI_4    246
#define KEYCODE_TV_INPUT_COMPOSITE_1 247
#define KEYCODE_TV_INPUT_COMPOSITE_2 248
#define KEYCODE_TV_INPUT_COMPONENT_1 249
#define KEYCODE_TV_INPUT_COMPONENT_2 250
#define KEYCODE_TV_INPUT_VGA_1     251

#define DIR_START_LONG 1
#define DIR_END_LONG   2
#define DIR_SHORT      3

typedef struct {
    const char *name;
    int keycode;
} key_mapping_t;

static const key_mapping_t s_key_map[] = {
    {"Up", KEYCODE_DPAD_UP},
    {"DirectionUp", KEYCODE_DPAD_UP},
    {"Down", KEYCODE_DPAD_DOWN},
    {"DirectionDown", KEYCODE_DPAD_DOWN},
    {"Left", KEYCODE_DPAD_LEFT},
    {"DirectionLeft", KEYCODE_DPAD_LEFT},
    {"Right", KEYCODE_DPAD_RIGHT},
    {"DirectionRight", KEYCODE_DPAD_RIGHT},
    {"Select", KEYCODE_DPAD_CENTER},
    {"OK", KEYCODE_DPAD_CENTER},
    {"Center", KEYCODE_DPAD_CENTER},
    {"Enter", KEYCODE_ENTER},
    {"Home", KEYCODE_HOME},
    {"Back", KEYCODE_BACK},
    {"Menu", KEYCODE_MENU},
    {"Power", KEYCODE_POWER},
    {"PowerOn", KEYCODE_POWER},
    {"PowerOff", KEYCODE_POWER},
    {"PowerToggle", KEYCODE_POWER},
    {"TvPower", KEYCODE_TV_POWER},
    {"VolumeUp", KEYCODE_VOLUME_UP},
    {"VolumeDown", KEYCODE_VOLUME_DOWN},
    {"Mute", KEYCODE_MUTE},
    {"MuteToggle", KEYCODE_MUTE},
    {"VolumeMute", KEYCODE_MUTE},
    {"Play", KEYCODE_MEDIA_PLAY},
    {"Pause", KEYCODE_MEDIA_PAUSE},
    {"PlayPause", KEYCODE_MEDIA_PLAY_PAUSE},
    {"Stop", KEYCODE_MEDIA_STOP},
    {"NextTrack", KEYCODE_MEDIA_NEXT},
    {"Next", KEYCODE_MEDIA_NEXT},
    {"SkipForward", KEYCODE_MEDIA_NEXT},
    {"PreviousTrack", KEYCODE_MEDIA_PREVIOUS},
    {"Previous", KEYCODE_MEDIA_PREVIOUS},
    {"Prev", KEYCODE_MEDIA_PREVIOUS},
    {"SkipBack", KEYCODE_MEDIA_PREVIOUS},
    {"Rewind", KEYCODE_MEDIA_REWIND},
    {"FastForward", KEYCODE_MEDIA_FAST_FORWARD},
    {"Fwd", KEYCODE_MEDIA_FAST_FORWARD},
    {"Record", KEYCODE_MEDIA_RECORD},
    {"ChannelUp", KEYCODE_CHANNEL_UP},
    {"ChannelDown", KEYCODE_CHANNEL_DOWN},
    {"PrevChannel", KEYCODE_LAST_CHANNEL},
    {"Search", KEYCODE_SEARCH},
    {"Settings", KEYCODE_SETTINGS},
    {"Input", KEYCODE_TV_INPUT},
    {"Source", KEYCODE_TV_INPUT},
    {"InputNext", KEYCODE_TV_INPUT},
    {"HDMI", KEYCODE_TV_INPUT_HDMI_1},
    {"HDMI1", KEYCODE_TV_INPUT_HDMI_1},
    {"HDMI2", KEYCODE_TV_INPUT_HDMI_2},
    {"HDMI3", KEYCODE_TV_INPUT_HDMI_3},
    {"HDMI4", KEYCODE_TV_INPUT_HDMI_4},
    {"InputHdmi", KEYCODE_TV_INPUT_HDMI_1},
    {"InputHdmi1", KEYCODE_TV_INPUT_HDMI_1},
    {"InputHdmi2", KEYCODE_TV_INPUT_HDMI_2},
    {"InputHdmi3", KEYCODE_TV_INPUT_HDMI_3},
    {"InputHdmi4", KEYCODE_TV_INPUT_HDMI_4},
    {"InputHDMI", KEYCODE_TV_INPUT_HDMI_1},
    {"InputHDMI1", KEYCODE_TV_INPUT_HDMI_1},
    {"InputHDMI2", KEYCODE_TV_INPUT_HDMI_2},
    {"InputHDMI3", KEYCODE_TV_INPUT_HDMI_3},
    {"InputHDMI4", KEYCODE_TV_INPUT_HDMI_4},
    {"Composite", KEYCODE_TV_INPUT_COMPOSITE_1},
    {"Composite1", KEYCODE_TV_INPUT_COMPOSITE_1},
    {"InputComposite", KEYCODE_TV_INPUT_COMPOSITE_1},
    {"Component", KEYCODE_TV_INPUT_COMPONENT_1},
    {"Component1", KEYCODE_TV_INPUT_COMPONENT_1},
    {"InputComponent", KEYCODE_TV_INPUT_COMPONENT_1},
    {"VGA", KEYCODE_TV_INPUT_VGA_1},
    {"InputVGA", KEYCODE_TV_INPUT_VGA_1},
    {"TV", KEYCODE_TV},
    {"DVR", KEYCODE_DVR},
    {"Guide", KEYCODE_GUIDE},
    {"Info", KEYCODE_INFO},
    {"Captions", KEYCODE_CAPTIONS},
    {"Subtitles", KEYCODE_CAPTIONS},
    {"Red", KEYCODE_PROG_RED},
    {"Green", KEYCODE_PROG_GREEN},
    {"Yellow", KEYCODE_PROG_YELLOW},
    {"Blue", KEYCODE_PROG_BLUE},
    {"AppSwitcher", KEYCODE_APP_SWITCH},
    {"AppSwitch", KEYCODE_APP_SWITCH},
    {"0", KEYCODE_0},
    {"Number0", KEYCODE_0},
    {"1", KEYCODE_1},
    {"Number1", KEYCODE_1},
    {"2", KEYCODE_2},
    {"Number2", KEYCODE_2},
    {"3", KEYCODE_3},
    {"Number3", KEYCODE_3},
    {"4", KEYCODE_4},
    {"Number4", KEYCODE_4},
    {"5", KEYCODE_5},
    {"Number5", KEYCODE_5},
    {"6", KEYCODE_6},
    {"Number6", KEYCODE_6},
    {"7", KEYCODE_7},
    {"Number7", KEYCODE_7},
    {"8", KEYCODE_8},
    {"Number8", KEYCODE_8},
    {"9", KEYCODE_9},
    {"Number9", KEYCODE_9},
    {"Delete", KEYCODE_DEL},
    {"Backspace", KEYCODE_DEL},
    {"Notifications", KEYCODE_NOTIFICATION},
    {"Pair", KEYCODE_PAIRING},
    {NULL, 0}
};

static int map_google_tv_keycode(const char *cmd) {
    if (!cmd || !cmd[0]) return KEYCODE_UNKNOWN;

    for (int i = 0; s_key_map[i].name != NULL; i++) {
        if (strcasecmp(cmd, s_key_map[i].name) == 0) {
            return s_key_map[i].keycode;
        }
    }

    if (strncasecmp(cmd, "KEY_", 4) == 0) {
        const char *sub = cmd + 4;
        for (int i = 0; s_key_map[i].name != NULL; i++) {
            if (strcasecmp(sub, s_key_map[i].name) == 0) {
                return s_key_map[i].keycode;
            }
        }
    }

    if (isdigit((unsigned char)cmd[0])) {
        return atoi(cmd);
    }

    return KEYCODE_UNKNOWN;
}

/* =========================================================================
 * Protobuf Varint and Framing Utilities
 * ========================================================================= */
static size_t encode_varint(unsigned char *buf, uint64_t val) {
    size_t len = 0;
    while (val >= 0x80) {
        buf[len++] = (unsigned char)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    buf[len++] = (unsigned char)(val & 0x7F);
    return len;
}

/* Build RemoteKeyInject (Field 10) */
static size_t build_remote_key_inject(unsigned char *out, size_t out_sz, int keycode, int direction) {
    unsigned char payload[32];
    size_t plen = 0;

    payload[plen++] = 0x08;
    plen += encode_varint(&payload[plen], (uint64_t)keycode);

    payload[plen++] = 0x10;
    plen += encode_varint(&payload[plen], (uint64_t)direction);

    size_t total = 0;
    out[total++] = 0x52;
    total += encode_varint(&out[total], plen);
    if (total + plen > out_sz) return 0;
    memcpy(&out[total], payload, plen);
    total += plen;
    return total;
}

/* Build RemoteAppLinkLaunchRequest (Field 90) */
static size_t build_remote_app_link(unsigned char *out, size_t out_sz, const char *app_link) {
    size_t link_len = strlen(app_link);
    unsigned char payload[512];
    size_t plen = 0;

    payload[plen++] = 0x0A;
    plen += encode_varint(&payload[plen], link_len);
    memcpy(&payload[plen], app_link, link_len);
    plen += link_len;

    size_t total = 0;
    out[total++] = 0xD2;
    out[total++] = 0x05;
    total += encode_varint(&out[total], plen);
    if (total + plen > out_sz) return 0;
    memcpy(&out[total], payload, plen);
    total += plen;
    return total;
}

/* Build RemoteImeBatchEdit (Field 21) for Text Input */
static size_t build_remote_ime_text(unsigned char *out, size_t out_sz, const char *text) {
    size_t tlen = strlen(text);
    unsigned char ime_obj[512];
    size_t obj_len = 0;

    ime_obj[obj_len++] = 0x08;
    obj_len += encode_varint(&ime_obj[obj_len], tlen);
    ime_obj[obj_len++] = 0x10;
    obj_len += encode_varint(&ime_obj[obj_len], tlen);
    ime_obj[obj_len++] = 0x1A;
    obj_len += encode_varint(&ime_obj[obj_len], tlen);
    memcpy(&ime_obj[obj_len], text, tlen);
    obj_len += tlen;

    unsigned char edit_info[512];
    size_t edit_len = 0;
    edit_info[edit_len++] = 0x08;
    edit_len += encode_varint(&edit_info[edit_len], 1);
    edit_info[edit_len++] = 0x12;
    edit_len += encode_varint(&edit_info[edit_len], obj_len);
    memcpy(&edit_info[edit_len], ime_obj, obj_len);
    edit_len += obj_len;

    unsigned char batch[1024];
    size_t batch_len = 0;
    batch[batch_len++] = 0x08;
    batch_len += encode_varint(&batch[batch_len], 1);
    batch[batch_len++] = 0x10;
    batch_len += encode_varint(&batch[batch_len], 1);
    batch[batch_len++] = 0x1A;
    batch_len += encode_varint(&batch[batch_len], edit_len);
    memcpy(&batch[batch_len], edit_info, edit_len);
    batch_len += edit_len;

    size_t total = 0;
    out[total++] = 0xAA;
    out[total++] = 0x01;
    total += encode_varint(&out[total], batch_len);
    if (total + batch_len > out_sz) return 0;
    memcpy(&out[total], batch, batch_len);
    total += batch_len;
    return total;
}

/* Build RemotePingResponse (Field 9) */
static size_t build_ping_response(unsigned char *out, size_t out_sz, uint64_t val1) {
    unsigned char inner[16];
    size_t ilen = 0;
    inner[ilen++] = 0x08;
    ilen += encode_varint(&inner[ilen], val1);

    unsigned char proto[32];
    size_t plen = 0;
    proto[plen++] = 0x4A;
    plen += encode_varint(&proto[plen], ilen);
    memcpy(&proto[plen], inner, ilen);
    plen += ilen;

    size_t flen = encode_varint(out, plen);
    if (flen + plen > out_sz) return 0;
    memcpy(out + flen, proto, plen);
    return flen + plen;
}

/* =========================================================================
 * BearSSL Permissive X.509 Validator & TLS Engine
 * ========================================================================= */
typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dec;
    br_x509_pkey pkey;
    int first_cert;
    uint8_t rsa_n[512];
    size_t rsa_nlen;
    uint8_t rsa_e[16];
    size_t rsa_elen;
} permissive_x509_context;

static void px509_start_chain(const br_x509_class **ctx, const char *server_name) {
    permissive_x509_context *pc = (permissive_x509_context *)ctx;
    (void)server_name;
    pc->first_cert = 1;
}

static void px509_start_cert(const br_x509_class **ctx, uint32_t length) {
    permissive_x509_context *pc = (permissive_x509_context *)ctx;
    (void)length;
    if (pc->first_cert) {
        br_x509_decoder_init(&pc->dec, 0, 0);
    }
}

static void px509_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    permissive_x509_context *pc = (permissive_x509_context *)ctx;
    if (pc->first_cert) {
        br_x509_decoder_push(&pc->dec, buf, len);
    }
}

static void px509_end_cert(const br_x509_class **ctx) {
    permissive_x509_context *pc = (permissive_x509_context *)ctx;
    if (pc->first_cert) {
        if (br_x509_decoder_last_error(&pc->dec) == 0) {
            const br_x509_pkey *pk = br_x509_decoder_get_pkey(&pc->dec);
            if (pk) {
                pc->pkey = *pk;
                if (pk->key_type == BR_KEYTYPE_RSA) {
                    if (pk->key.rsa.n && pk->key.rsa.nlen > 0 && pk->key.rsa.nlen <= sizeof(pc->rsa_n)) {
                        memcpy(pc->rsa_n, pk->key.rsa.n, pk->key.rsa.nlen);
                        pc->rsa_nlen = pk->key.rsa.nlen;
                    }
                    if (pk->key.rsa.e && pk->key.rsa.elen > 0 && pk->key.rsa.elen <= sizeof(pc->rsa_e)) {
                        memcpy(pc->rsa_e, pk->key.rsa.e, pk->key.rsa.elen);
                        pc->rsa_elen = pk->key.rsa.elen;
                    }
                }
            }
        }
        pc->first_cert = 0;
    }
}

static unsigned px509_end_chain(const br_x509_class **ctx) {
    (void)ctx;
    return 0;
}

static const br_x509_pkey *px509_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    permissive_x509_context *pc = (permissive_x509_context *)ctx;
    if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return &pc->pkey;
}

static const br_x509_class px509_vtable = {
    sizeof(permissive_x509_context),
    px509_start_chain,
    px509_start_cert,
    px509_append,
    px509_end_cert,
    px509_end_chain,
    px509_get_pkey
};

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

static int read_delimited_msg(br_sslio_context *ioc, unsigned char *out, size_t out_max) {
    uint64_t msg_len = 0;
    int shift = 0;
    for (;;) {
        unsigned char b;
        if (br_sslio_read_all(ioc, &b, 1) != 0) return -1;
        msg_len |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift > 35) return -1;
    }
    if (msg_len == 0 || msg_len > out_max) return -1;
    if (br_sslio_read_all(ioc, out, (size_t)msg_len) != 0) return -1;
    return (int)msg_len;
}

/* =========================================================================
 * Control Channel (Port 6466)
 * ========================================================================= */
static const uint8_t s_remote_configure[] = {
    0x40, 0x0a, 0x3e, 0x08, 0xee, 0x04, 0x12, 0x39,
    0x0a, 0x0b, 'H', 'a', 'r', 'm', 'o', 'n', 'y', ' ', 'H', 'u', 'b',
    0x12, 0x08, 'L', 'o', 'g', 'i', 't', 'e', 'c', 'h',
    0x18, 0x01,
    0x22, 0x01, '1',
    0x2a, 0x14, 'c', 'o', 'm', '.', 'l', 'o', 'g', 'i', 't', 'e', 'c', 'h', '.', 'h', 'a', 'r', 'm', 'o', 'n', 'y',
    0x32, 0x05, '1', '.', '0', '.', '0'
};

static const uint8_t s_remote_set_active[] = {
    0x05, 0x12, 0x03, 0x08, 0xee, 0x04
};

typedef struct {
    int fd;
    br_ssl_client_context sc;
    br_sslio_context ioc;
    permissive_x509_context px509;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    char host[64];
    int connected;
} ctl_session_t;

static ctl_session_t s_ctl;

static void ctl_disconnect(void) {
    if (s_ctl.connected || s_ctl.fd >= 0) {
        br_ssl_engine_close(&s_ctl.sc.eng);
        if (s_ctl.fd >= 0) close(s_ctl.fd);
        s_ctl.fd = -1;
        s_ctl.connected = 0;
        s_ctl.host[0] = '\0';
    }
}

static void init_bearssl_client_trimmed(br_ssl_client_context *cc) {
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_RSA_WITH_AES_128_GCM_SHA256
    };
    br_ssl_client_zero(cc);
    br_ssl_engine_set_versions(&cc->eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(&cc->eng, suites, (sizeof suites) / (sizeof suites[0]));
    br_ssl_client_set_default_rsapub(cc);
    br_ssl_engine_set_default_rsavrfy(&cc->eng);
    br_ssl_engine_set_ec(&cc->eng, &br_ec_prime_i31);
    br_ssl_engine_set_default_aes_gcm(&cc->eng);
    br_ssl_engine_set_hash(&cc->eng, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);
}

static int ctl_connect(const char *host) {
    if (!host || !host[0]) return -1;
    if (s_ctl.connected && strcmp(s_ctl.host, host) == 0) return 0;
    ctl_disconnect();

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(ATV_PORT_CONTROL);
    if (inet_pton(AF_INET, host, &sin.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

    /* Non-blocking connect with 1.5s timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int cr = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
    if (cr != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    if (cr != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 500000 };
        if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
            close(fd);
            return -1;
        }
        int so_err = 0;
        socklen_t slen = sizeof(so_err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen);
        if (so_err != 0) {
            close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL, flags);

    struct timeval rtv = { .tv_sec = 1, .tv_usec = 500000 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    memset(&s_ctl.px509, 0, sizeof(s_ctl.px509));
    s_ctl.px509.vtable = &px509_vtable;

    br_x509_certificate chain[1];
    chain[0].data = cert_der;
    chain[0].data_len = cert_der_len;

    br_skey_decoder_context skdc;
    br_skey_decoder_init(&skdc);
    br_skey_decoder_push(&skdc, key_der, key_der_len);

    init_bearssl_client_trimmed(&s_ctl.sc);
    br_ssl_engine_set_x509(&s_ctl.sc.eng, &s_ctl.px509.vtable);
    br_ssl_engine_set_buffer(&s_ctl.sc.eng, s_ctl.iobuf, sizeof(s_ctl.iobuf), 1);

    if (br_skey_decoder_last_error(&skdc) == 0 && br_skey_decoder_key_type(&skdc) == BR_KEYTYPE_RSA) {
        const br_rsa_private_key *sk = br_skey_decoder_get_rsa(&skdc);
        br_ssl_client_set_single_rsa(&s_ctl.sc, chain, 1, sk, br_rsa_pkcs1_sign_get_default());
    }

    s_ctl.fd = fd;
    br_ssl_client_reset(&s_ctl.sc, host, 0);
    br_sslio_init(&s_ctl.ioc, &s_ctl.sc.eng, sock_read, &s_ctl.fd, sock_write, &s_ctl.fd);

    /* Handshake on 6466:
     * 1. Read RemoteConfigure from TV
     * 2. Send our RemoteConfigure
     * 3. Read RemoteSetActive from TV
     * 4. Send our RemoteSetActive
     */
    unsigned char tmp[512];
    int r = read_delimited_msg(&s_ctl.ioc, tmp, sizeof(tmp));
    if (r <= 0) {
        ctl_disconnect();
        return -1;
    }

    if (br_sslio_write_all(&s_ctl.ioc, s_remote_configure, sizeof(s_remote_configure)) != 0 ||
        br_sslio_flush(&s_ctl.ioc) != 0) {
        ctl_disconnect();
        return -1;
    }

    r = read_delimited_msg(&s_ctl.ioc, tmp, sizeof(tmp));
    if (r <= 0) {
        ctl_disconnect();
        return -1;
    }

    if (br_sslio_write_all(&s_ctl.ioc, s_remote_set_active, sizeof(s_remote_set_active)) != 0 ||
        br_sslio_flush(&s_ctl.ioc) != 0) {
        ctl_disconnect();
        return -1;
    }

    s_ctl.connected = 1;
    strncpy(s_ctl.host, host, sizeof(s_ctl.host) - 1);
    printf("[+] android-tv: Control session connected to %s:%d\n", host, ATV_PORT_CONTROL);
    return 0;
}

/* Handle incoming packets on s_ctl (e.g. RemotePingRequest) */
static int handle_ctl_read(void) {
    if (!s_ctl.connected || s_ctl.fd < 0) return -1;

    unsigned char buf[512];
    int rlen = read_delimited_msg(&s_ctl.ioc, buf, sizeof(buf));
    if (rlen <= 0) return -1;

    /* Parse RemoteMessage fields */
    size_t i = 0;
    while (i < (size_t)rlen) {
        uint64_t tag = 0;
        int shift = 0;
        while (i < (size_t)rlen) {
            unsigned char b = buf[i++];
            tag |= (uint64_t)(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        int field = (int)(tag >> 3);
        int wire = (int)(tag & 0x07);

        if (wire == 0) {
            while (i < (size_t)rlen && (buf[i++] & 0x80) != 0);
        } else if (wire == 2) {
            uint64_t len = 0;
            shift = 0;
            while (i < (size_t)rlen) {
                unsigned char b = buf[i++];
                len |= (uint64_t)(b & 0x7F) << shift;
                if ((b & 0x80) == 0) break;
                shift += 7;
            }
            if (field == 8 && i + len <= (size_t)rlen) {
                /* RemotePingRequest (Field 8): parse val1 (Field 1, tag 0x08) */
                size_t p = i;
                uint64_t val1 = 0;
                while (p < i + len && (buf[p++] & 0x80) != 0);
                shift = 0;
                while (p < i + len) {
                    unsigned char b = buf[p++];
                    val1 |= (uint64_t)(b & 0x7F) << shift;
                    if ((b & 0x80) == 0) break;
                    shift += 7;
                }
                /* Echo back RemotePingResponse (Field 9) */
                unsigned char resp[32];
                size_t rlen_resp = build_ping_response(resp, sizeof(resp), val1);
                if (rlen_resp > 0) {
                    br_sslio_write_all(&s_ctl.ioc, resp, rlen_resp);
                    br_sslio_flush(&s_ctl.ioc);
                }
            }
            i += len;
        } else {
            break;
        }
    }
    return 0;
}

static int transmit_payload(const char *host, const unsigned char *payload, size_t payload_len) {
    if (!host || !host[0] || !payload || payload_len == 0) return -1;

    unsigned char frame[2048];
    size_t hdr_len = encode_varint(frame, payload_len);
    if (hdr_len + payload_len > sizeof(frame)) return -1;
    memcpy(&frame[hdr_len], payload, payload_len);
    size_t frame_total = hdr_len + payload_len;

    for (int attempt = 0; attempt < 2; attempt++) {
        if (!s_ctl.connected || strcmp(s_ctl.host, host) != 0) {
            if (ctl_connect(host) != 0) return -1;
        }

        if (br_sslio_write_all(&s_ctl.ioc, frame, frame_total) == 0 &&
            br_sslio_flush(&s_ctl.ioc) == 0) {
            return 0;
        }

        printf("[-] android-tv: Control write failed, reconnecting...\n");
        ctl_disconnect();
    }
    return -1;
}

static int send_google_tv_key(const char *host, int keycode, int direction) {
    unsigned char buf[128];
    size_t len = build_remote_key_inject(buf, sizeof(buf), keycode, direction);
    if (len == 0) return -1;
    return transmit_payload(host, buf, len);
}

static int send_google_tv_text(const char *host, const char *text) {
    unsigned char buf[1024];
    size_t len = build_remote_ime_text(buf, sizeof(buf), text);
    if (len == 0) return -1;
    return transmit_payload(host, buf, len);
}

static int send_google_tv_app_link(const char *host, const char *link) {
    char full_link[256];
    if (strcasecmp(link, "youtube") == 0) strcpy(full_link, "https://www.youtube.com");
    else if (strcasecmp(link, "netflix") == 0) strcpy(full_link, "android-app://com.netflix.ninja");
    else if (strcasecmp(link, "prime") == 0 || strcasecmp(link, "primevideo") == 0) strcpy(full_link, "android-app://com.amazon.amazonvideo.livingroom");
    else if (strcasecmp(link, "disney") == 0 || strcasecmp(link, "disney+") == 0) strcpy(full_link, "android-app://com.disney.disneyplus");
    else if (strcasecmp(link, "spotify") == 0) strcpy(full_link, "android-app://com.spotify.tv.android");
    else if (strcasecmp(link, "plex") == 0) strcpy(full_link, "android-app://com.plexapp.android");
    else if (strcasecmp(link, "kodi") == 0) strcpy(full_link, "android-app://org.xbmc.kodi");
    else if (strcasecmp(link, "vlc") == 0) strcpy(full_link, "android-app://org.videolan.vlc");
    else if (strcasecmp(link, "twitch") == 0) strcpy(full_link, "android-app://tv.twitch.android.app");
    else if (strstr(link, "://") != NULL) strncpy(full_link, link, sizeof(full_link) - 1);
    else if (strstr(link, ".") != NULL) snprintf(full_link, sizeof(full_link), "android-app://%s", link);
    else strncpy(full_link, link, sizeof(full_link) - 1);

    unsigned char buf[1024];
    size_t len = build_remote_app_link(buf, sizeof(buf), full_link);
    if (len == 0) return -1;
    return transmit_payload(host, buf, len);
}

static int send_channel_digits(const char *host, const char *digits) {
    if (!digits) return -1;
    for (size_t i = 0; digits[i]; i++) {
        char d = digits[i];
        if (d >= '0' && d <= '9') {
            int kcode = KEYCODE_0 + (d - '0');
            send_google_tv_key(host, kcode, DIR_SHORT);
            usleep(100000);
        }
    }
    return 0;
}

/* =========================================================================
 * State persistence
 * ========================================================================= */
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

static void write_status_file(const char *action, const char *stage, const char *msg, int ok, int paired, const char *host) {
    mkdir("/tmp/plugins", 0755);
    FILE *f = fopen("/tmp/plugins/android-tv.status", "w");
    if (!f) return;
    fprintf(f, "{\n"
               "  \"ok\": %s,\n"
               "  \"action\": \"%s\",\n"
               "  \"stage\": \"%s\",\n"
               "  \"message\": \"%s\",\n"
               "  \"paired\": %s,\n"
               "  \"host\": \"%s\",\n"
               "  \"port\": %d\n"
               "}\n",
               ok ? "true" : "false",
               action ? action : "",
               stage ? stage : "",
               msg ? msg : "",
               paired ? "true" : "false",
               host ? host : "",
               ATV_PORT_CONTROL);
    fclose(f);
}

/* =========================================================================
 * POLO Pairing Implementation (Single-Threaded, No Pthreads)
 * Wire format matches Google TV / Android TV Remote Protocol v2 (Port 6467)
 * ========================================================================= */
static const uint8_t s_polo_pairing_request[] = {
    0x1f,
    0x08, 0x02,
    0x10, 0xc8, 0x01,
    0x52, 0x18,
    0x0a, 0x09, 'a','t','v','r','e','m','o','t','e',
    0x12, 0x0b, 'H','a','r','m','o','n','y',' ','H','u','b'
};

static const uint8_t s_polo_options[] = {
    0x10,
    0x08, 0x02,
    0x10, 0xc8, 0x01,
    0xa2, 0x01, 0x08,
    0x0a, 0x04, 0x08, 0x03, 0x10, 0x06,
    0x18, 0x01
};

static const uint8_t s_polo_configuration[] = {
    0x10,
    0x08, 0x02,
    0x10, 0xc8, 0x01,
    0xf2, 0x01, 0x08,
    0x0a, 0x04, 0x08, 0x03, 0x10, 0x06,
    0x10, 0x01
};

typedef struct {
    int fd;
    br_ssl_client_context sc;
    br_sslio_context ioc;
    permissive_x509_context px509;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    char host[64];
    time_t started_at;
    int active;
} polo_session_t;

static polo_session_t s_polo;

static void polo_close(void) {
    if (s_polo.active || s_polo.fd >= 0) {
        br_ssl_engine_close(&s_polo.sc.eng);
        if (s_polo.fd >= 0) close(s_polo.fd);
        s_polo.fd = -1;
        s_polo.active = 0;
        s_polo.host[0] = '\0';
    }
}

static const uint8_t *strip_leading_zero(const uint8_t *data, size_t *len) {
    while (*len > 1 && *data == 0x00) {
        data++;
        (*len)--;
    }
    return data;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_polo_status_ok(const unsigned char *buf, size_t len) {
    if (len < 5) return 0;
    for (size_t i = 0; i + 2 < len; i++) {
        if (buf[i] == 0x10 && buf[i+1] == 0xC8 && buf[i+2] == 0x01) {
            return 1;
        }
    }
    return 0;
}

static void do_start_pair(const char *host) {
    polo_close();

    if (!host || !host[0]) {
        write_status_file("start", "error", "Host IP required", 0, 0, "");
        return;
    }

    printf("[*] android-tv: Initiating POLO pairing with %s:%d...\n", host, ATV_PORT_PAIR);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        write_status_file("start", "error", "Failed to create TCP socket", 0, 0, host);
        return;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(ATV_PORT_PAIR);
    if (inet_pton(AF_INET, host, &sin.sin_addr) <= 0) {
        write_status_file("start", "error", "Invalid IP address", 0, 0, host);
        close(fd);
        return;
    }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int cr = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
    if (cr != 0 && errno != EINPROGRESS) {
        write_status_file("start", "error", "Failed to connect to device", 0, 0, host);
        close(fd);
        return;
    }
    if (cr != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
            write_status_file("start", "error", "Connection timed out", 0, 0, host);
            close(fd);
            return;
        }
        int so_err = 0;
        socklen_t slen = sizeof(so_err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen);
        if (so_err != 0) {
            write_status_file("start", "error", "Failed to connect to device", 0, 0, host);
            close(fd);
            return;
        }
    }
    fcntl(fd, F_SETFL, flags);

    struct timeval rtv = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    memset(&s_polo.px509, 0, sizeof(s_polo.px509));
    s_polo.px509.vtable = &px509_vtable;

    br_x509_certificate chain[1];
    chain[0].data = cert_der;
    chain[0].data_len = cert_der_len;

    br_skey_decoder_context skdc;
    br_skey_decoder_init(&skdc);
    br_skey_decoder_push(&skdc, key_der, key_der_len);

    init_bearssl_client_trimmed(&s_polo.sc);
    br_ssl_engine_set_x509(&s_polo.sc.eng, &s_polo.px509.vtable);
    br_ssl_engine_set_buffer(&s_polo.sc.eng, s_polo.iobuf, sizeof(s_polo.iobuf), 1);

    if (br_skey_decoder_last_error(&skdc) == 0 && br_skey_decoder_key_type(&skdc) == BR_KEYTYPE_RSA) {
        const br_rsa_private_key *sk = br_skey_decoder_get_rsa(&skdc);
        br_ssl_client_set_single_rsa(&s_polo.sc, chain, 1, sk, br_rsa_pkcs1_sign_get_default());
    }

    s_polo.fd = fd;
    br_ssl_client_reset(&s_polo.sc, host, 0);
    br_sslio_init(&s_polo.ioc, &s_polo.sc.eng, sock_read, &s_polo.fd, sock_write, &s_polo.fd);

    /* 1. PairingRequest */
    printf("[*] android-tv: Sending PairingRequest...\n");
    if (br_sslio_write_all(&s_polo.ioc, s_polo_pairing_request, sizeof(s_polo_pairing_request)) != 0 ||
        br_sslio_flush(&s_polo.ioc) != 0) {
        write_status_file("start", "error", "Failed to write PairingRequest", 0, 0, host);
        polo_close();
        return;
    }

    unsigned char msg_buf[512];
    int rlen = read_delimited_msg(&s_polo.ioc, msg_buf, sizeof(msg_buf));
    if (rlen <= 0 || !is_polo_status_ok(msg_buf, (size_t)rlen)) {
        write_status_file("start", "error", "PairingRequest rejected by device", 0, 0, host);
        polo_close();
        return;
    }

    /* 2. Options */
    printf("[*] android-tv: Sending Options...\n");
    if (br_sslio_write_all(&s_polo.ioc, s_polo_options, sizeof(s_polo_options)) != 0 ||
        br_sslio_flush(&s_polo.ioc) != 0) {
        write_status_file("start", "error", "Failed to write Options", 0, 0, host);
        polo_close();
        return;
    }

    rlen = read_delimited_msg(&s_polo.ioc, msg_buf, sizeof(msg_buf));
    if (rlen <= 0 || !is_polo_status_ok(msg_buf, (size_t)rlen)) {
        write_status_file("start", "error", "Options rejected by device", 0, 0, host);
        polo_close();
        return;
    }

    /* 3. Configuration */
    printf("[*] android-tv: Sending Configuration...\n");
    if (br_sslio_write_all(&s_polo.ioc, s_polo_configuration, sizeof(s_polo_configuration)) != 0 ||
        br_sslio_flush(&s_polo.ioc) != 0) {
        write_status_file("start", "error", "Failed to write Configuration", 0, 0, host);
        polo_close();
        return;
    }

    rlen = read_delimited_msg(&s_polo.ioc, msg_buf, sizeof(msg_buf));
    if (rlen <= 0 || !is_polo_status_ok(msg_buf, (size_t)rlen)) {
        write_status_file("start", "error", "Configuration rejected by device", 0, 0, host);
        polo_close();
        return;
    }

    printf("[+] android-tv: ConfigurationAck received! PIN should now be visible on TV screen.\n");
    s_polo.active = 1;
    s_polo.started_at = time(NULL);
    strncpy(s_polo.host, host, sizeof(s_polo.host) - 1);
    write_status_file("start", "need_pin", "PIN displayed on TV screen", 1, 0, host);
}

static void do_finish_pair(const char *pin_code) {
    if (!s_polo.active || s_polo.fd < 0) {
        printf("[-] android-tv: No pairing session active\n");
        write_status_file("pin", "error", "No pairing session active", 0, 0, s_paired_host);
        return;
    }

    if (time(NULL) - s_polo.started_at > 90) {
        printf("[-] android-tv: Pairing session expired\n");
        write_status_file("pin", "error", "Pairing session expired", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    char clean_pin[32] = {0};
    size_t cpi = 0;
    for (size_t i = 0; pin_code[i] && cpi < sizeof(clean_pin) - 1; i++) {
        if (!isspace((unsigned char)pin_code[i])) {
            clean_pin[cpi++] = pin_code[i];
        }
    }
    clean_pin[cpi] = '\0';

    if (strlen(clean_pin) != 6) {
        printf("[-] android-tv: Invalid PIN length %zu\n", strlen(clean_pin));
        write_status_file("pin", "error", "PIN must be exactly 6 characters", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    for (int i = 0; i < 6; i++) {
        if (hex_nibble(clean_pin[i]) < 0) {
            printf("[-] android-tv: Invalid non-hex character in PIN '%s'\n", clean_pin);
            write_status_file("pin", "error", "PIN must be hexadecimal", 0, 0, s_polo.host);
            polo_close();
            return;
        }
    }

    /* Decode client certificate public key */
    br_x509_decoder_context dec;
    br_x509_decoder_init(&dec, 0, 0);
    br_x509_decoder_push(&dec, cert_der, cert_der_len);
    const br_x509_pkey *cli_pk = br_x509_decoder_get_pkey(&dec);
    if (!cli_pk || cli_pk->key_type != BR_KEYTYPE_RSA) {
        write_status_file("pin", "error", "Client key is not RSA", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    size_t cli_nlen = cli_pk->key.rsa.nlen;
    const uint8_t *cli_n = strip_leading_zero(cli_pk->key.rsa.n, &cli_nlen);
    size_t cli_elen = cli_pk->key.rsa.elen;
    const uint8_t *cli_e = strip_leading_zero(cli_pk->key.rsa.e, &cli_elen);

    size_t srv_nlen = s_polo.px509.rsa_nlen;
    const uint8_t *srv_n = strip_leading_zero(s_polo.px509.rsa_n, &srv_nlen);
    size_t srv_elen = s_polo.px509.rsa_elen;
    const uint8_t *srv_e = strip_leading_zero(s_polo.px509.rsa_e, &srv_elen);

    if (srv_nlen == 0 || srv_elen == 0) {
        write_status_file("pin", "error", "Server certificate key missing", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    uint8_t pin_bytes[2];
    pin_bytes[0] = (uint8_t)((hex_nibble(clean_pin[2]) << 4) | hex_nibble(clean_pin[3]));
    pin_bytes[1] = (uint8_t)((hex_nibble(clean_pin[4]) << 4) | hex_nibble(clean_pin[5]));
    uint8_t expected_first = (uint8_t)((hex_nibble(clean_pin[0]) << 4) | hex_nibble(clean_pin[1]));

    br_sha256_context hc;
    br_sha256_init(&hc);
    br_sha256_update(&hc, cli_n, cli_nlen);
    br_sha256_update(&hc, cli_e, cli_elen);
    br_sha256_update(&hc, srv_n, srv_nlen);
    br_sha256_update(&hc, srv_e, srv_elen);
    br_sha256_update(&hc, pin_bytes, 2);
    uint8_t hash[32];
    br_sha256_out(&hc, hash);

    if (hash[0] != expected_first) {
        printf("[-] android-tv: Secret hash mismatch: got 0x%02X expected 0x%02X\n", hash[0], expected_first);
        write_status_file("pin", "error", "Incorrect PIN code entered", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    uint8_t secret_frame[43];
    secret_frame[0] = 42;
    secret_frame[1] = 0x08;
    secret_frame[2] = 0x02;
    secret_frame[3] = 0x10;
    secret_frame[4] = 0xc8;
    secret_frame[5] = 0x01;
    secret_frame[6] = 0xc2;
    secret_frame[7] = 0x02;
    secret_frame[8] = 0x22;
    secret_frame[9] = 0x0a;
    secret_frame[10] = 0x20;
    memcpy(&secret_frame[11], hash, 32);

    printf("[*] android-tv: Sending SecretMessage with verified hash...\n");
    if (br_sslio_write_all(&s_polo.ioc, secret_frame, sizeof(secret_frame)) != 0 ||
        br_sslio_flush(&s_polo.ioc) != 0) {
        write_status_file("pin", "error", "Failed to transmit secret challenge", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    unsigned char msg_buf[512];
    int rlen = read_delimited_msg(&s_polo.ioc, msg_buf, sizeof(msg_buf));
    if (rlen <= 0 || !is_polo_status_ok(msg_buf, (size_t)rlen)) {
        write_status_file("pin", "error", "Device rejected PIN or Secret challenge", 0, 0, s_polo.host);
        polo_close();
        return;
    }

    char target_host[64];
    strncpy(target_host, s_polo.host, sizeof(target_host) - 1);
    target_host[sizeof(target_host) - 1] = '\0';
    polo_close();

    printf("[+] android-tv: Pairing successful with %s!\n", target_host);
    save_state(target_host, 1);
    write_status_file("pin", "done", "Pairing completed successfully", 1, 1, target_host);

    ctl_disconnect();
    ctl_connect(target_host);
}

/* =========================================================================
 * JSON Helpers
 * ========================================================================= */
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

/* =========================================================================
 * Main Daemon Loop
 * ========================================================================= */
int main(int argc, char **argv) {
    s_ctl.fd = -1;
    s_polo.fd = -1;
    (void)argc;
    (void)argv;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    setvbuf(stdout, NULL, _IONBF, 0);

    const char *sock_path = getenv("HARMONY_SOCKET");
    if (!sock_path || !sock_path[0]) {
        if (argc > 1 && argv[1] && argv[1][0]) sock_path = argv[1];
        else sock_path = "/tmp/plugins/android-tv.sock";
    }

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
    printf("[*] android-tv (Google TV Remote v2) daemon ready on %s\n", sock_path);

    /* Pre-connect to paired host if configured */
    if (s_is_paired && s_paired_host[0]) {
        ctl_connect(s_paired_host);
    }

    char buf[4096];
    struct pollfd pfd[2];
    while (s_running) {
        int nfds = 1;
        pfd[0].fd = sfd;
        pfd[0].events = POLLIN;

        if (s_ctl.connected && s_ctl.fd >= 0) {
            pfd[1].fd = s_ctl.fd;
            pfd[1].events = POLLIN;
            nfds = 2;
        }

        int pr = poll(pfd, nfds, 1000);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* 1. Handle incoming message or disconnect from TV (pings, etc.) */
        if (nfds > 1 && (pfd[1].revents & (POLLIN | POLLERR | POLLHUP))) {
            if (handle_ctl_read() != 0) {
                printf("[*] android-tv: Control socket closed by TV, will reconnect on next command\n");
                ctl_disconnect();
            }
        }

        /* 2. Handle incoming datagram from Hub daemon */
        if (!(pfd[0].revents & POLLIN)) continue;

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

        char raw_cmd[256] = {0};
        extract_json_str(buf, "command", raw_cmd, sizeof(raw_cmd));

        char action[32] = "tap";
        extract_json_str(buf, "action", action, sizeof(action));
        if (!action[0]) strcpy(action, "tap");

        char pin[32] = {0};
        extract_json_str(buf, "pin", pin, sizeof(pin));

        /* 1. Pairing: PIN Verification / Finish */
        if (strcasecmp(action, "finish_pair") == 0 || strcasecmp(action, "pin") == 0) {
            printf("[*] android-tv: Received PIN '%s' for pairing...\n", pin);
            do_finish_pair(pin);
            continue;
        }

        /* 2. Pairing: Start */
        if (strcasecmp(action, "start_pair") == 0 || strcasecmp(action, "start") == 0 ||
            strcasecmp(raw_cmd, "Pair") == 0) {
            do_start_pair(host);
            continue;
        }

        if (!host[0]) continue;

        /* Home Assistant direction prefix: SHORT:, START_LONG:, END_LONG: */
        int direction = DIR_SHORT;
        if (strcmp(action, "press") == 0 || strcmp(action, "repeat") == 0) direction = DIR_START_LONG;
        else if (strcmp(action, "release") == 0) direction = DIR_END_LONG;

        const char *cmd = raw_cmd;
        if (strncasecmp(cmd, "SHORT:", 6) == 0) {
            direction = DIR_SHORT;
            cmd += 6;
        } else if (strncasecmp(cmd, "START_LONG:", 11) == 0) {
            direction = DIR_START_LONG;
            cmd += 11;
        } else if (strncasecmp(cmd, "END_LONG:", 9) == 0) {
            direction = DIR_END_LONG;
            cmd += 9;
        }

        /* Text Entry: text:<string> */
        if (strncasecmp(cmd, "text:", 5) == 0) {
            const char *text = cmd + 5;
            printf("[*] android-tv: Sending text '%s' to %s:%d\n", text, host, ATV_PORT_CONTROL);
            send_google_tv_text(host, text);
            continue;
        }

        /* App Launch / Deep link */
        if (strncasecmp(cmd, "app:", 4) == 0) {
            const char *app = cmd + 4;
            printf("[*] android-tv: Launching app '%s' on %s:%d\n", app, host, ATV_PORT_CONTROL);
            send_google_tv_app_link(host, app);
            continue;
        }
        if (strncasecmp(cmd, "launch:", 7) == 0) {
            const char *app = cmd + 7;
            printf("[*] android-tv: Launching app '%s' on %s:%d\n", app, host, ATV_PORT_CONTROL);
            send_google_tv_app_link(host, app);
            continue;
        }
        if (strncasecmp(cmd, "http://", 7) == 0 || strncasecmp(cmd, "https://", 8) == 0) {
            printf("[*] android-tv: Launching URL '%s' on %s:%d\n", cmd, host, ATV_PORT_CONTROL);
            send_google_tv_app_link(host, cmd);
            continue;
        }

        /* Channel Digits: channel:<digits> */
        if (strncasecmp(cmd, "channel:", 8) == 0) {
            const char *ch = cmd + 8;
            printf("[*] android-tv: Switching channel '%s' on %s:%d\n", ch, host, ATV_PORT_CONTROL);
            send_channel_digits(host, ch);
            continue;
        }

        /* Keycode command */
        int kcode = map_google_tv_keycode(cmd);
        if (kcode == KEYCODE_UNKNOWN) {
            printf("[-] android-tv: Unknown/unsupported command '%s', ignoring\n", cmd);
            continue;
        }
        printf("[*] android-tv: Keycode %d ('%s', dir=%d) -> %s:%d\n", kcode, cmd, direction, host, ATV_PORT_CONTROL);
        send_google_tv_key(host, kcode, direction);
    }

    polo_close();
    ctl_disconnect();
    close(sfd);
    unlink(sock_path);
    printf("[*] android-tv daemon stopped\n");
    return 0;
}
