/* Host self-check of the address check and the message parsing:
 *   gcc ps5-wake/test_ps5_wake.c -o /tmp/t && /tmp/t
 */
#include <assert.h>
#define main ps5_main
#include "ps5_wake.c"
#undef main

int main(void) {
    assert(valid_mac("84:e6:57:C2:0D:97"));
    assert(!valid_mac(""));
    assert(!valid_mac("84:E6:57:C2:0D:9"));
    assert(!valid_mac("84:E6:57:C2:0D:977"));
    assert(!valid_mac("84-E6-57-C2-0D-97"));
    assert(!valid_mac("x';reboot;'0D:97"));
    assert(!valid_mac("84:E6:57:C2:0D:9'"));

    const char *msg = "{\"command\":\"Wake\",\"action\":\"press\",\"value\":0,"
                      "\"config\":{\"controller_mac\":\"24:A6:FA:A0:1C:E5\"}}";
    char out[32];
    extract_json_str(msg, "command", out, sizeof(out));
    assert(strcmp(out, "Wake") == 0);
    extract_json_str(msg, "value", out, sizeof(out));
    assert(strcmp(out, "0") == 0);
    /* The "mac" fallback does not pick up controller_mac */
    extract_json_str(msg, "mac", out, sizeof(out));
    assert(out[0] == '\0');
    extract_json_str(msg, "controller_mac", out, sizeof(out));
    assert(strcmp(out, "24:A6:FA:A0:1C:E5") == 0);
    /* Cut at the buffer size, never past it */
    extract_json_str("{\"host_mac\":\"84:E6:57:C2:0D:97:AA:BB:CC:DD:EE\"}", "host_mac", out, sizeof(out));
    assert(strlen(out) == sizeof(out) - 1 && !valid_mac(out));

    printf("[+] ps5-wake self-check passed\n");
    return 0;
}
