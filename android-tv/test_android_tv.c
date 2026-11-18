/* Host self-check: key names, app links, and the generated client key and certificate.
 * Build BearSSL for the host once, then:
 *   gcc -I<bearssl>/inc android-tv/test_android_tv.c <host-libbearssl.a> -o /tmp/t && /tmp/t /tmp/atv_id
 * With a directory argument it writes key.der and cert.der there for an openssl check.
 */
#include <assert.h>
#define main atv_main
#include "android_tv.c"
#undef main

static void check_link(const char *in, const char *want) {
    char out[384];
    assert(resolve_app_link(in, out, sizeof(out)) == 0);
    if (strcmp(out, want) != 0) {
        fprintf(stderr, "link '%s' -> '%s', want '%s'\n", in, out, want);
        assert(0);
    }
}

int main(int argc, char **argv) {
    assert(map_google_tv_keycode("HDMI 1") == KEYCODE_TV_INPUT_HDMI_1);
    assert(map_google_tv_keycode("hdmi_4") == KEYCODE_TV_INPUT_HDMI_4);
    assert(map_google_tv_keycode("VolumeMute") == KEYCODE_VOLUME_MUTE);
    assert(map_google_tv_keycode("PowerOn") == KEYCODE_WAKEUP);
    assert(map_google_tv_keycode("PowerOff") == KEYCODE_SLEEP);
    assert(map_google_tv_keycode("PowerToggle") == KEYCODE_POWER);
    assert(map_google_tv_keycode("LastChannel") == KEYCODE_LAST_CHANNEL);
    assert(map_google_tv_keycode("Rev") == KEYCODE_MEDIA_REWIND);
    assert(map_google_tv_keycode("Assist") == KEYCODE_ASSIST);
    assert(map_google_tv_keycode("KEY_HOME") == KEYCODE_HOME);
    assert(map_google_tv_keycode("5") == KEYCODE_5);
    assert(map_google_tv_keycode("keycode:85") == 85);
    assert(map_google_tv_keycode("3D") == KEYCODE_UNKNOWN);
    assert(map_google_tv_keycode("AudioTrack") == 222);
    assert(map_google_tv_keycode("Button 4") == 191);

    check_link("netflix", "market://launch?id=com.netflix.ninja");
    check_link("Disney+", "market://launch?id=com.disney.disneyplus");
    check_link("com.plexapp.android", "market://launch?id=com.plexapp.android");
    check_link("android-app://org.xbmc.kodi", "market://launch?id=org.xbmc.kodi");
    check_link("youtube", "https://www.youtube.com");
    check_link("https://www.netflix.com/title/80057281", "https://www.netflix.com/title/80057281");
    check_link("market://launch?id=com.x.y", "market://launch?id=com.x.y");

    /* Datagram fields: commas, escapes and nested config inside values */
    char v[256];
    const char *dg = "{\"command\":\"app:https://x.tv/a?b=1,2&c=\\\"q\\\"\\/\\u00e9\",\"action\":\"tap\","
                     "\"ts\":12345,\"config\":{\"host\":\"10.0.0.5\"}}";
    extract_json_str(dg, "command", v, sizeof(v));
    assert(strcmp(v, "app:https://x.tv/a?b=1,2&c=\"q\"/\xc3\xa9") == 0);
    extract_json_str(dg, "ts", v, sizeof(v));
    assert(strcmp(v, "12345") == 0);
    extract_json_str(dg, "host", v, sizeof(v));
    assert(strcmp(v, "10.0.0.5") == 0);
    extract_json_str(dg, "pin", v, sizeof(v));
    assert(v[0] == '\0');

    /* Client identity: the certificate parses, holds the generated key and is self-signed */
    assert(generate_identity() == 0);
    br_x509_decoder_context dec;
    br_x509_decoder_init(&dec, 0, 0);
    br_x509_decoder_push(&dec, s_cert, s_cert_len);
    assert(br_x509_decoder_last_error(&dec) == 0);
    const br_x509_pkey *pk = br_x509_decoder_get_pkey(&dec);
    assert(pk && pk->key_type == BR_KEYTYPE_RSA && pk->key.rsa.nlen == ATV_KEY_BITS / 8);
    br_skey_decoder_context skd;
    br_skey_decoder_init(&skd);
    br_skey_decoder_push(&skd, s_key, s_key_len);
    assert(br_skey_decoder_last_error(&skd) == 0 && br_skey_decoder_key_type(&skd) == BR_KEYTYPE_RSA);
    const br_rsa_private_key *sk = br_skey_decoder_get_rsa(&skd);
    unsigned char msg[32] = { 1, 2, 3 }, sig[ATV_KEY_BITS / 8], out[32];
    assert(br_rsa_i31_pkcs1_sign(BR_HASH_OID_SHA256, msg, 32, sk, sig));
    br_rsa_public_key cpk = { (unsigned char *)pk->key.rsa.n, pk->key.rsa.nlen,
                              (unsigned char *)pk->key.rsa.e, pk->key.rsa.elen };
    assert(br_rsa_i31_pkcs1_vrfy(sig, sizeof(sig), BR_HASH_OID_SHA256, 32, &cpk, out));
    assert(memcmp(out, msg, 32) == 0);
    if (argc > 1) {
        char path[256];
        snprintf(path, sizeof(path), "%s/key.der", argv[1]);
        assert(write_blob(path, s_key, s_key_len, 0600) == 0);
        snprintf(path, sizeof(path), "%s/cert.der", argv[1]);
        assert(write_blob(path, s_cert, s_cert_len, 0644) == 0);
    }

    printf("android_tv self-check OK\n");
    return 0;
}
