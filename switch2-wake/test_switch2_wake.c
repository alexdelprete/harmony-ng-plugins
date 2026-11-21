/* Host self-check of the wake packet, the wake report parser, the address and product id
 * checks and the parsing:
 *   gcc switch2-wake/test_switch2_wake.c -o /tmp/t && /tmp/t
 */
#include <assert.h>
#define main switch2_main
#include "switch2_wake.c"
#undef main

int main(void) {
    /* Byte-exact vector from alexvnesta/switch2controller docs/research/wake-packet.md */
    static const uint8_t expect[WAKE_ADV_LEN] = {
        0x02, 0x01, 0x06, 0x1B, 0xFF, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7E, 0x05, 0x66, 0x20,
        0x00, 0x01, 0x81, 0x97, 0x1F, 0x99, 0x55, 0xE2, 0x98, 0x0F, 0, 0, 0, 0, 0, 0, 0,
    };
    uint8_t sw[6], adv[WAKE_ADV_LEN];
    assert(valid_mac("98:E2:55:99:1F:97"));
    parse_mac("98:E2:55:99:1F:97", sw);
    build_wake_adv(adv, sw, 0x2066);
    assert(memcmp(adv, expect, sizeof(expect)) == 0);
    build_wake_adv(adv, sw, 0x2069);
    assert(adv[12] == 0x69 && adv[13] == 0x20);

    /* The controller's own wake advertisement gives the console and the product id back */
    uint8_t got[6];
    uint16_t pid = 0;
    assert(parse_wake_report(expect, WAKE_ADV_LEN, got, &pid));
    assert(memcmp(got, sw, 6) == 0 && pid == 0x2066);
    uint8_t ad[WAKE_ADV_LEN];
    memcpy(ad, expect, sizeof(ad));
    ad[16] = 0x00; /* a pairing advertisement */
    assert(!parse_wake_report(ad, WAKE_ADV_LEN, got, &pid));
    assert(!parse_wake_report(expect, 20, got, &pid));
    memcpy(ad, expect, sizeof(ad));
    ad[5] = 0x54; /* another company */
    assert(!parse_wake_report(ad, WAKE_ADV_LEN, got, &pid));

    uint8_t hx[31];
    assert(parse_hex("0201061BFF", hx, 31) == 5 && hx[3] == 0x1B);
    assert(parse_hex("020", hx, 31) == -1 && parse_hex("zz", hx, 31) == -1);
    assert(parse_hex("00", hx, 0) == -1);

    assert(!valid_mac(""));
    assert(!valid_mac("98:E2:55:99:1F:9"));
    assert(!valid_mac("98-E2-55-99-1F-97"));
    assert(!valid_mac("98:E2:55:99:1F:9g"));

    assert(parse_pid("") == 0x2066);
    assert(parse_pid("2067") == 0x2067);
    assert(parse_pid("0x2069") == 0x2069);
    assert(parse_pid("2068") == -1);
    assert(parse_pid("20669") == -1);
    assert(parse_pid("zz66") == -1);

    const char *msg = "{\"command\":\"Wake\",\"action\":\"press\",\"config\":"
                      "{\"switch_mac\":\"98:E2:55:99:1F:97\",\"controller_mac\":\"AA:BB:CC:DD:EE:FF\"}}";
    char out[80];
    extract_json_str(msg, "switch_mac", out, sizeof(out));
    assert(strcmp(out, "98:E2:55:99:1F:97") == 0);
    extract_json_str(msg, "controller_pid", out, sizeof(out));
    assert(out[0] == '\0');

    /* A report from hub_btstack: "addr" is not taken from "addr_type" */
    const char *rep = "{\"plugin_id\":\"switch2-wake\",\"command\":\"adv_report\",\"action\":\"tap\",\"value\":0,"
                      "\"addr\":\"11:22:33:44:55:66\",\"addr_type\":0,\"event_type\":3,\"rssi\":-60,\"data\":\"020106\"}";
    extract_json_str(rep, "addr", out, sizeof(out));
    assert(strcmp(out, "11:22:33:44:55:66") == 0);
    extract_json_str(rep, "addr_type", out, sizeof(out));
    assert(strcmp(out, "0") == 0);

    printf("[+] switch2-wake self-check passed\n");
    return 0;
}
