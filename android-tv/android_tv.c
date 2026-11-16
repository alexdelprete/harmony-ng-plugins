#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "bearssl.h"
#include "atv_cert_data.h"

static volatile int s_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    s_running = 0;
}

/* =========================================================================
 * Google TV Remote v2 (Android TV) Keycodes
 * Matching Home Assistant components/androidtv_remote and TvKeys
 * ========================================================================= */
#define KEYCODE_UNKNOWN             0
#define KEYCODE_HOME                3
#define KEYCODE_BACK                4
#define KEYCODE_0                   7
#define KEYCODE_1                   8
#define KEYCODE_2                   9
#define KEYCODE_3                  10
#define KEYCODE_4                  11
#define KEYCODE_5                  12
#define KEYCODE_6                  13
#define KEYCODE_7                  14
#define KEYCODE_8                  15
#define KEYCODE_9                  16
#define KEYCODE_DPAD_UP            19
#define KEYCODE_DPAD_DOWN          20
#define KEYCODE_DPAD_LEFT          21
#define KEYCODE_DPAD_RIGHT         22
#define KEYCODE_DPAD_CENTER        23
#define KEYCODE_VOLUME_UP          24
#define KEYCODE_VOLUME_DOWN        25
#define KEYCODE_POWER              26
#define KEYCODE_ENTER              66
#define KEYCODE_DEL                67
#define KEYCODE_MENU               82
#define KEYCODE_NOTIFICATION      83
#define KEYCODE_SEARCH             84
#define KEYCODE_MEDIA_PLAY_PAUSE   85
#define KEYCODE_MEDIA_STOP         86
#define KEYCODE_MEDIA_NEXT         87
#define KEYCODE_MEDIA_PREVIOUS     88
#define KEYCODE_MEDIA_REWIND       89
#define KEYCODE_MEDIA_FAST_FORWARD 90
#define KEYCODE_MUTE               91
#define KEYCODE_PAGE_UP            92
#define KEYCODE_PAGE_DOWN          93
#define KEYCODE_BUTTON_A           96
#define KEYCODE_BUTTON_B           97
#define KEYCODE_BUTTON_X           99
#define KEYCODE_BUTTON_Y          100
#define KEYCODE_BUTTON_L1         102
#define KEYCODE_BUTTON_R1         103
#define KEYCODE_BUTTON_L2         104
#define KEYCODE_BUTTON_R2         105
#define KEYCODE_BUTTON_START      108
#define KEYCODE_BUTTON_SELECT     109
#define KEYCODE_ESCAPE            111
#define KEYCODE_FORWARD           125
#define KEYCODE_MEDIA_PLAY        126
#define KEYCODE_MEDIA_PAUSE       127
#define KEYCODE_MEDIA_CLOSE       128
#define KEYCODE_MEDIA_EJECT       129
#define KEYCODE_MEDIA_RECORD      130
#define KEYCODE_VOLUME_MUTE       164
#define KEYCODE_INFO              165
#define KEYCODE_CHANNEL_UP        166
#define KEYCODE_CHANNEL_DOWN      167
#define KEYCODE_TV                170
#define KEYCODE_GUIDE             172
#define KEYCODE_DVR               173
#define KEYCODE_CAPTIONS          175
#define KEYCODE_SETTINGS          176
#define KEYCODE_TV_POWER          177
#define KEYCODE_TV_INPUT          178
#define KEYCODE_PROG_RED          183
#define KEYCODE_PROG_GREEN        184
#define KEYCODE_PROG_YELLOW       185
#define KEYCODE_PROG_BLUE         186
#define KEYCODE_APP_SWITCH        187
#define KEYCODE_BUTTON_1          188
#define KEYCODE_BUTTON_2          189
#define KEYCODE_BUTTON_3          190
#define KEYCODE_BUTTON_4          191
#define KEYCODE_ASSIST            219
#define KEYCODE_BRIGHTNESS_DOWN   220
#define KEYCODE_BRIGHTNESS_UP     221
#define KEYCODE_MEDIA_AUDIO_TRACK 222
#define KEYCODE_SLEEP             223
#define KEYCODE_WAKEUP            224
#define KEYCODE_PAIRING           225
#define KEYCODE_MEDIA_TOP_MENU    226
#define KEYCODE_11                227
#define KEYCODE_12                228
#define KEYCODE_LAST_CHANNEL      229
#define KEYCODE_TV_DATA_SERVICE   230
#define KEYCODE_VOICE_ASSIST      231
#define KEYCODE_TV_TELETEXT       233
#define KEYCODE_TV_NUMBER_ENTRY   234
#define KEYCODE_TV_INPUT_HDMI_1   243
#define KEYCODE_TV_INPUT_HDMI_2   244
#define KEYCODE_TV_INPUT_HDMI_3   245
#define KEYCODE_TV_INPUT_HDMI_4   246
#define KEYCODE_TV_INPUT_COMPOSITE_1 247
#define KEYCODE_TV_INPUT_COMPOSITE_2 248
#define KEYCODE_TV_INPUT_COMPONENT_1 249
#define KEYCODE_TV_INPUT_COMPONENT_2 250
#define KEYCODE_TV_INPUT_VGA_1    251
#define KEYCODE_TV_ZOOM_MODE      255
#define KEYCODE_HELP              259
#define KEYCODE_MEDIA_SKIP_FORWARD 272
#define KEYCODE_MEDIA_SKIP_BACKWARD 273
#define KEYCODE_MEDIA_STEP_FORWARD 274
#define KEYCODE_MEDIA_STEP_BACKWARD 275
#define KEYCODE_SOFT_SLEEP        276
#define KEYCODE_ALL_APPS          284
#define KEYCODE_REFRESH           285
#define KEYCODE_THUMBS_UP         286
#define KEYCODE_THUMBS_DOWN       287
#define KEYCODE_VIDEO_APP_1       289 /* Netflix */
#define KEYCODE_VIDEO_APP_2       290 /* YouTube */
#define KEYCODE_VIDEO_APP_3       291 /* Prime Video */
#define KEYCODE_VIDEO_APP_4       292 /* Disney+ */
#define KEYCODE_FEATURED_APP_1    297

/* Direction values matching RemoteDirection in remotemessage.proto */
#define DIR_UNKNOWN    0
#define DIR_START_LONG 1
#define DIR_END_LONG   2
#define DIR_SHORT      3

/* =========================================================================
 * Command Mapping (Comprehensive Home Assistant & TvKeys Parity)
 * ========================================================================= */
static int map_google_tv_keycode(const char *cmd) {
    if (!cmd || !cmd[0]) return KEYCODE_DPAD_CENTER;

    /* Direct KEYCODE_ string prefix handling */
    if (strncasecmp(cmd, "KEYCODE_", 8) == 0) {
        cmd += 8;
    }

    /* Navigation & D-Pad */
    if (strcasecmp(cmd, "Up") == 0 || strcasecmp(cmd, "DPAD_UP") == 0 || strcasecmp(cmd, "DirectionUp") == 0) return KEYCODE_DPAD_UP;
    if (strcasecmp(cmd, "Down") == 0 || strcasecmp(cmd, "DPAD_DOWN") == 0 || strcasecmp(cmd, "DirectionDown") == 0) return KEYCODE_DPAD_DOWN;
    if (strcasecmp(cmd, "Left") == 0 || strcasecmp(cmd, "DPAD_LEFT") == 0 || strcasecmp(cmd, "DirectionLeft") == 0) return KEYCODE_DPAD_LEFT;
    if (strcasecmp(cmd, "Right") == 0 || strcasecmp(cmd, "DPAD_RIGHT") == 0 || strcasecmp(cmd, "DirectionRight") == 0) return KEYCODE_DPAD_RIGHT;
    if (strcasecmp(cmd, "Select") == 0 || strcasecmp(cmd, "OK") == 0 || strcasecmp(cmd, "Enter") == 0 || strcasecmp(cmd, "DPAD_CENTER") == 0 || strcasecmp(cmd, "CENTER") == 0) return KEYCODE_DPAD_CENTER;
    if (strcasecmp(cmd, "Back") == 0 || strcasecmp(cmd, "Return") == 0 || strcasecmp(cmd, "ESCAPE") == 0) return KEYCODE_BACK;
    if (strcasecmp(cmd, "Home") == 0) return KEYCODE_HOME;
    if (strcasecmp(cmd, "Menu") == 0) return KEYCODE_MENU;
    if (strcasecmp(cmd, "Settings") == 0) return KEYCODE_SETTINGS;
    if (strcasecmp(cmd, "Search") == 0) return KEYCODE_SEARCH;
    if (strcasecmp(cmd, "Assist") == 0 || strcasecmp(cmd, "VoiceAssist") == 0 || strcasecmp(cmd, "VOICE_ASSIST") == 0) return KEYCODE_ASSIST;
    if (strcasecmp(cmd, "Info") == 0) return KEYCODE_INFO;
    if (strcasecmp(cmd, "Guide") == 0) return KEYCODE_GUIDE;
    if (strcasecmp(cmd, "Captions") == 0 || strcasecmp(cmd, "Subtitles") == 0) return KEYCODE_CAPTIONS;
    if (strcasecmp(cmd, "Notifications") == 0 || strcasecmp(cmd, "Notification") == 0) return KEYCODE_NOTIFICATION;
    if (strcasecmp(cmd, "AppSwitch") == 0 || strcasecmp(cmd, "APP_SWITCH") == 0 || strcasecmp(cmd, "TaskSwitcher") == 0) return KEYCODE_APP_SWITCH;
    if (strcasecmp(cmd, "AllApps") == 0 || strcasecmp(cmd, "ALL_APPS") == 0) return KEYCODE_ALL_APPS;

    /* Power Controls */
    if (strcasecmp(cmd, "Power") == 0 || strcasecmp(cmd, "PowerToggle") == 0 || strcasecmp(cmd, "POWER_TOGGLE") == 0) return KEYCODE_POWER;
    if (strcasecmp(cmd, "PowerOn") == 0 || strcasecmp(cmd, "Wakeup") == 0 || strcasecmp(cmd, "WAKE_UP") == 0) return KEYCODE_WAKEUP;
    if (strcasecmp(cmd, "PowerOff") == 0 || strcasecmp(cmd, "Sleep") == 0 || strcasecmp(cmd, "SOFT_SLEEP") == 0) return KEYCODE_SLEEP;
    if (strcasecmp(cmd, "TvPower") == 0 || strcasecmp(cmd, "TV_POWER") == 0) return KEYCODE_TV_POWER;

    /* Volume & Audio */
    if (strcasecmp(cmd, "VolumeUp") == 0 || strcasecmp(cmd, "VOLUME_UP") == 0) return KEYCODE_VOLUME_UP;
    if (strcasecmp(cmd, "VolumeDown") == 0 || strcasecmp(cmd, "VOLUME_DOWN") == 0) return KEYCODE_VOLUME_DOWN;
    if (strcasecmp(cmd, "VolumeMute") == 0 || strcasecmp(cmd, "Mute") == 0 || strcasecmp(cmd, "VOLUME_MUTE") == 0) return KEYCODE_VOLUME_MUTE;
    if (strcasecmp(cmd, "AudioTrack") == 0 || strcasecmp(cmd, "MEDIA_AUDIO_TRACK") == 0) return KEYCODE_MEDIA_AUDIO_TRACK;

    /* Media Playback */
    if (strcasecmp(cmd, "Play") == 0 || strcasecmp(cmd, "MEDIA_PLAY") == 0) return KEYCODE_MEDIA_PLAY;
    if (strcasecmp(cmd, "Pause") == 0 || strcasecmp(cmd, "MEDIA_PAUSE") == 0) return KEYCODE_MEDIA_PAUSE;
    if (strcasecmp(cmd, "PlayPause") == 0 || strcasecmp(cmd, "MEDIA_PLAY_PAUSE") == 0) return KEYCODE_MEDIA_PLAY_PAUSE;
    if (strcasecmp(cmd, "Stop") == 0 || strcasecmp(cmd, "MEDIA_STOP") == 0) return KEYCODE_MEDIA_STOP;
    if (strcasecmp(cmd, "Next") == 0 || strcasecmp(cmd, "NextTrack") == 0 || strcasecmp(cmd, "MEDIA_NEXT") == 0) return KEYCODE_MEDIA_NEXT;
    if (strcasecmp(cmd, "Prev") == 0 || strcasecmp(cmd, "Previous") == 0 || strcasecmp(cmd, "PrevTrack") == 0 || strcasecmp(cmd, "MEDIA_PREVIOUS") == 0) return KEYCODE_MEDIA_PREVIOUS;
    if (strcasecmp(cmd, "Fwd") == 0 || strcasecmp(cmd, "FastForward") == 0 || strcasecmp(cmd, "MEDIA_FAST_FORWARD") == 0 || strcasecmp(cmd, "FAST_FORWARD") == 0) return KEYCODE_MEDIA_FAST_FORWARD;
    if (strcasecmp(cmd, "Rev") == 0 || strcasecmp(cmd, "Rewind") == 0 || strcasecmp(cmd, "MEDIA_REWIND") == 0 || strcasecmp(cmd, "REWIND") == 0) return KEYCODE_MEDIA_REWIND;
    if (strcasecmp(cmd, "Record") == 0 || strcasecmp(cmd, "MEDIA_RECORD") == 0) return KEYCODE_MEDIA_RECORD;
    if (strcasecmp(cmd, "TopMenu") == 0 || strcasecmp(cmd, "MEDIA_TOP_MENU") == 0) return KEYCODE_MEDIA_TOP_MENU;
    if (strcasecmp(cmd, "SkipForward") == 0 || strcasecmp(cmd, "MEDIA_SKIP_FORWARD") == 0) return KEYCODE_MEDIA_SKIP_FORWARD;
    if (strcasecmp(cmd, "SkipBackward") == 0 || strcasecmp(cmd, "MEDIA_SKIP_BACKWARD") == 0) return KEYCODE_MEDIA_SKIP_BACKWARD;

    /* Channels & Numeric Keypad */
    if (strcmp(cmd, "0") == 0) return KEYCODE_0;
    if (strcmp(cmd, "1") == 0) return KEYCODE_1;
    if (strcmp(cmd, "2") == 0) return KEYCODE_2;
    if (strcmp(cmd, "3") == 0) return KEYCODE_3;
    if (strcmp(cmd, "4") == 0) return KEYCODE_4;
    if (strcmp(cmd, "5") == 0) return KEYCODE_5;
    if (strcmp(cmd, "6") == 0) return KEYCODE_6;
    if (strcmp(cmd, "7") == 0) return KEYCODE_7;
    if (strcmp(cmd, "8") == 0) return KEYCODE_8;
    if (strcmp(cmd, "9") == 0) return KEYCODE_9;
    if (strcmp(cmd, "11") == 0) return KEYCODE_11;
    if (strcmp(cmd, "12") == 0) return KEYCODE_12;
    if (strcasecmp(cmd, "ChannelUp") == 0 || strcasecmp(cmd, "CHANNEL_UP") == 0) return KEYCODE_CHANNEL_UP;
    if (strcasecmp(cmd, "ChannelDown") == 0 || strcasecmp(cmd, "CHANNEL_DOWN") == 0) return KEYCODE_CHANNEL_DOWN;
    if (strcasecmp(cmd, "LastChannel") == 0 || strcasecmp(cmd, "PrevChannel") == 0 || strcasecmp(cmd, "LAST_CHANNEL") == 0) return KEYCODE_LAST_CHANNEL;

    /* Color Buttons */
    if (strcasecmp(cmd, "Red") == 0 || strcasecmp(cmd, "ColorRed") == 0 || strcasecmp(cmd, "PROG_RED") == 0) return KEYCODE_PROG_RED;
    if (strcasecmp(cmd, "Green") == 0 || strcasecmp(cmd, "ColorGreen") == 0 || strcasecmp(cmd, "PROG_GREEN") == 0) return KEYCODE_PROG_GREEN;
    if (strcasecmp(cmd, "Yellow") == 0 || strcasecmp(cmd, "ColorYellow") == 0 || strcasecmp(cmd, "PROG_YELLOW") == 0) return KEYCODE_PROG_YELLOW;
    if (strcasecmp(cmd, "Blue") == 0 || strcasecmp(cmd, "ColorBlue") == 0 || strcasecmp(cmd, "PROG_BLUE") == 0) return KEYCODE_PROG_BLUE;

    /* Inputs */
    if (strcasecmp(cmd, "Input") == 0 || strcasecmp(cmd, "TvInput") == 0 || strcasecmp(cmd, "TV_INPUT") == 0) return KEYCODE_TV_INPUT;
    if (strcasecmp(cmd, "HDMI 1") == 0 || strcasecmp(cmd, "HDMI1") == 0 || strcasecmp(cmd, "TV_INPUT_HDMI_1") == 0) return KEYCODE_TV_INPUT_HDMI_1;
    if (strcasecmp(cmd, "HDMI 2") == 0 || strcasecmp(cmd, "HDMI2") == 0 || strcasecmp(cmd, "TV_INPUT_HDMI_2") == 0) return KEYCODE_TV_INPUT_HDMI_2;
    if (strcasecmp(cmd, "HDMI 3") == 0 || strcasecmp(cmd, "HDMI3") == 0 || strcasecmp(cmd, "TV_INPUT_HDMI_3") == 0) return KEYCODE_TV_INPUT_HDMI_3;
    if (strcasecmp(cmd, "HDMI 4") == 0 || strcasecmp(cmd, "HDMI4") == 0 || strcasecmp(cmd, "TV_INPUT_HDMI_4") == 0) return KEYCODE_TV_INPUT_HDMI_4;
    if (strcasecmp(cmd, "Composite1") == 0 || strcasecmp(cmd, "TV_INPUT_COMPOSITE_1") == 0) return KEYCODE_TV_INPUT_COMPOSITE_1;
    if (strcasecmp(cmd, "Component1") == 0 || strcasecmp(cmd, "TV_INPUT_COMPONENT_1") == 0) return KEYCODE_TV_INPUT_COMPONENT_1;

    /* Dedicated App Buttons (Remote Hardware Keys) */
    if (strcasecmp(cmd, "Netflix") == 0 || strcasecmp(cmd, "VIDEO_APP_1") == 0) return KEYCODE_VIDEO_APP_1;
    if (strcasecmp(cmd, "YouTube") == 0 || strcasecmp(cmd, "VIDEO_APP_2") == 0) return KEYCODE_VIDEO_APP_2;
    if (strcasecmp(cmd, "PrimeVideo") == 0 || strcasecmp(cmd, "Prime") == 0 || strcasecmp(cmd, "VIDEO_APP_3") == 0) return KEYCODE_VIDEO_APP_3;
    if (strcasecmp(cmd, "Disney") == 0 || strcasecmp(cmd, "Disney+") == 0 || strcasecmp(cmd, "VIDEO_APP_4") == 0) return KEYCODE_VIDEO_APP_4;
    if (strcasecmp(cmd, "Button1") == 0 || strcasecmp(cmd, "BUTTON_1") == 0) return KEYCODE_BUTTON_1;
    if (strcasecmp(cmd, "Button2") == 0 || strcasecmp(cmd, "BUTTON_2") == 0) return KEYCODE_BUTTON_2;
    if (strcasecmp(cmd, "Button3") == 0 || strcasecmp(cmd, "BUTTON_3") == 0) return KEYCODE_BUTTON_3;
    if (strcasecmp(cmd, "Button4") == 0 || strcasecmp(cmd, "BUTTON_4") == 0) return KEYCODE_BUTTON_4;

    /* Fallback: parse numeric keycode */
    if (isdigit((unsigned char)cmd[0])) {
        return atoi(cmd);
    }

    return KEYCODE_DPAD_CENTER;
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

    /* Field 1 (key_code): tag 0x08 */
    payload[plen++] = 0x08;
    plen += encode_varint(&payload[plen], (uint64_t)keycode);

    /* Field 2 (direction): tag 0x10 */
    payload[plen++] = 0x10;
    plen += encode_varint(&payload[plen], (uint64_t)direction);

    /* Wrap in RemoteMessage: Field 10 (RemoteKeyInject), tag 0x52 */
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

    /* Field 1 (app_link, string): tag 0x0A */
    payload[plen++] = 0x0A;
    plen += encode_varint(&payload[plen], link_len);
    memcpy(&payload[plen], app_link, link_len);
    plen += link_len;

    /* Wrap in RemoteMessage: Field 90 (0xD2, 0x05) */
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

    /* RemoteImeObject:
     * Field 1 (start): tag 0x08, val = tlen
     * Field 2 (end): tag 0x10, val = tlen
     * Field 3 (value): tag 0x1A, len, text
     */
    ime_obj[obj_len++] = 0x08;
    obj_len += encode_varint(&ime_obj[obj_len], tlen);
    ime_obj[obj_len++] = 0x10;
    obj_len += encode_varint(&ime_obj[obj_len], tlen);
    ime_obj[obj_len++] = 0x1A;
    obj_len += encode_varint(&ime_obj[obj_len], tlen);
    memcpy(&ime_obj[obj_len], text, tlen);
    obj_len += tlen;

    /* RemoteEditInfo:
     * Field 1 (insert): tag 0x08, val = 1
     * Field 2 (text_field_status): tag 0x12, obj_len, ime_obj
     */
    unsigned char edit_info[512];
    size_t edit_len = 0;
    edit_info[edit_len++] = 0x08;
    edit_len += encode_varint(&edit_info[edit_len], 1);
    edit_info[edit_len++] = 0x12;
    edit_len += encode_varint(&edit_info[edit_len], obj_len);
    memcpy(&edit_info[edit_len], ime_obj, obj_len);
    edit_len += obj_len;

    /* RemoteImeBatchEdit:
     * Field 1 (ime_counter): tag 0x08, val = 1
     * Field 2 (field_counter): tag 0x10, val = 1
     * Field 3 (edit_info): tag 0x1A, edit_len, edit_info
     */
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

    /* Wrap in RemoteMessage: Field 21 (0xAA, 0x01) */
    size_t total = 0;
    out[total++] = 0xAA;
    out[total++] = 0x01;
    total += encode_varint(&out[total], batch_len);
    if (total + batch_len > out_sz) return 0;
    memcpy(&out[total], batch, batch_len);
    total += batch_len;
    return total;
}

/* =========================================================================
 * BearSSL Permissive X.509 Validator & TLS Engine
 * ========================================================================= */
typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dec;
    br_x509_pkey pkey;
    int first_cert;
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
            if (pk) pc->pkey = *pk;
        }
        pc->first_cert = 0;
    }
}

static unsigned px509_end_chain(const br_x509_class **ctx) {
    (void)ctx;
    return 0; /* Accept all certificates */
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

/* Connect and transmit a framed payload over BearSSL TLS (or plain TCP fallback) */
static int transmit_payload(const char *host, int port, const unsigned char *payload, size_t payload_len) {
    if (!host || !host[0] || !payload || payload_len == 0) return -1;
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

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        close(fd);
        return -1;
    }

    /* Wrap in Google TV varint length framing */
    unsigned char frame[2048];
    size_t hdr_len = encode_varint(frame, payload_len);
    if (hdr_len + payload_len > sizeof(frame)) {
        close(fd);
        return -1;
    }
    memcpy(&frame[hdr_len], payload, payload_len);
    size_t frame_total = hdr_len + payload_len;

    /* Initialize BearSSL TLS */
    br_ssl_client_context sc;
    br_sslio_context ioc;
    permissive_x509_context px509;
    px509.vtable = &px509_vtable;

    br_x509_certificate chain[1];
    chain[0].data = cert_der;
    chain[0].data_len = cert_der_len;

    br_skey_decoder_context skdc;
    br_skey_decoder_init(&skdc);
    br_skey_decoder_push(&skdc, key_der, key_der_len);

    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_x509_minimal_context xc;
    br_ssl_client_init_full(&sc, &xc, NULL, 0);
    br_ssl_engine_set_x509(&sc.eng, &px509.vtable);
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);

    if (br_skey_decoder_last_error(&skdc) == 0 && br_skey_decoder_key_type(&skdc) == BR_KEYTYPE_RSA) {
        const br_rsa_private_key *sk = br_skey_decoder_get_rsa(&skdc);
        br_ssl_client_set_single_rsa(&sc, chain, 1, sk, br_rsa_pkcs1_sign_get_default());
    }

    br_ssl_client_reset(&sc, host, 0);
    br_sslio_init(&ioc, &sc.eng, sock_read, &fd, sock_write, &fd);

    /* Write framed message over TLS */
    int ret = br_sslio_write_all(&ioc, frame, frame_total);
    if (ret == 0) {
        br_sslio_flush(&ioc);
    } else {
        /* Fallback: try raw send if peer does not negotiate TLS */
        send(fd, frame, frame_total, MSG_NOSIGNAL);
    }

    /* Brief grace sleep to ensure TCP buffers flush */
    usleep(15000);
    br_ssl_engine_close(&sc.eng);
    close(fd);
    return (ret == 0) ? 0 : -1;
}

/* Send a key command */
static int send_google_tv_key(const char *host, int port, int keycode, int direction) {
    unsigned char buf[128];
    size_t len = build_remote_key_inject(buf, sizeof(buf), keycode, direction);
    if (len == 0) return -1;
    return transmit_payload(host, port, buf, len);
}

/* Send text input */
static int send_google_tv_text(const char *host, int port, const char *text) {
    unsigned char buf[1024];
    size_t len = build_remote_ime_text(buf, sizeof(buf), text);
    if (len == 0) return -1;
    return transmit_payload(host, port, buf, len);
}

/* Send app link / deep link */
static int send_google_tv_app_link(const char *host, int port, const char *link) {
    /* Resolve common shorthands to full URI */
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
    return transmit_payload(host, port, buf, len);
}

/* Send multi-digit channel sequence */
static int send_channel_digits(const char *host, int port, const char *digits) {
    if (!digits) return -1;
    for (size_t i = 0; digits[i]; i++) {
        char d = digits[i];
        if (d >= '0' && d <= '9') {
            int kcode = KEYCODE_0 + (d - '0');
            send_google_tv_key(host, port, kcode, DIR_SHORT);
            usleep(100000); /* 100ms delay between digits */
        }
    }
    return 0;
}

/* State persistence */
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

/* JSON Extraction helpers */
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
    printf("[*] android-tv (Google TV Remote v2) daemon ready on %s\n", sock_path);
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

        char raw_cmd[256] = {0};
        extract_json_str(buf, "command", raw_cmd, sizeof(raw_cmd));

        char action[32] = "tap";
        extract_json_str(buf, "action", action, sizeof(action));
        if (!action[0]) strcpy(action, "tap");

        char pin[32] = {0};
        extract_json_str(buf, "pin", pin, sizeof(pin));

        /* Pairing actions */
        if (strcasecmp(raw_cmd, "Pair") == 0 || strcasecmp(action, "start_pair") == 0) {
            if (host[0]) {
                printf("[*] android-tv: Starting Google TV pairing on %s:6466...\n", host);
                save_state(host, 1);
            }
            continue;
        }

        if (!host[0]) continue;

        /* Parse Home Assistant direction prefix: SHORT:, START_LONG:, END_LONG: */
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

        /* 1. Text Entry: text:<string> */
        if (strncasecmp(cmd, "text:", 5) == 0) {
            const char *text = cmd + 5;
            printf("[*] android-tv: Sending text '%s' to %s:%d\n", text, host, port);
            send_google_tv_text(host, port, text);
            continue;
        }

        /* 2. App Launch / Deep link: app:<link> or launch:<link> or http:// or https:// */
        if (strncasecmp(cmd, "app:", 4) == 0) {
            const char *app = cmd + 4;
            printf("[*] android-tv: Launching app '%s' on %s:%d\n", app, host, port);
            send_google_tv_app_link(host, port, app);
            continue;
        }
        if (strncasecmp(cmd, "launch:", 7) == 0) {
            const char *app = cmd + 7;
            printf("[*] android-tv: Launching app '%s' on %s:%d\n", app, host, port);
            send_google_tv_app_link(host, port, app);
            continue;
        }
        if (strncasecmp(cmd, "http://", 7) == 0 || strncasecmp(cmd, "https://", 8) == 0) {
            printf("[*] android-tv: Launching URL '%s' on %s:%d\n", cmd, host, port);
            send_google_tv_app_link(host, port, cmd);
            continue;
        }

        /* 3. Channel Digits: channel:<digits> */
        if (strncasecmp(cmd, "channel:", 8) == 0) {
            const char *ch = cmd + 8;
            printf("[*] android-tv: Switching channel '%s' on %s:%d\n", ch, host, port);
            send_channel_digits(host, port, ch);
            continue;
        }

        /* 4. Keycode command */
        int kcode = map_google_tv_keycode(cmd);
        printf("[*] android-tv: Keycode %d (dir=%d) -> %s:%d\n", kcode, direction, host, port);
        send_google_tv_key(host, port, kcode, direction);
    }

    close(sfd);
    unlink(sock_path);
    printf("[*] android-tv daemon stopped\n");
    return 0;
}
