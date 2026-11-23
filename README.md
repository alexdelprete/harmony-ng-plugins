# HarmonyNG Official Community Plugins

Official plugins repository for [**HarmonyNG**](https://github.com/iranl/harmony-ng).

This repository contains official reference plugins, packaging tools, and the master `repository.json` catalog served to the Harmony Hub Marketplace.

---

## Included Reference Plugins

| Plugin | ID | Description | Device Type |
| :--- | :--- | :--- | :--- |
| **Roku ECP Controller** | `roku-ecp` | Control Roku streaming devices and Roku TVs over local Wi-Fi via External Control Protocol (ECP). | `roku` |
| **PlayStation 5 BT Wake** | `ps5-wake` | Wake PlayStation 4 / PlayStation 5 consoles over Bluetooth using DualSense / DualShock emulation. | `ps5` |
| **Nintendo Switch 2 BT Wake** | `switch2-wake` | Wake a Nintendo Switch 2 from sleep with the Bluetooth LE wake advertisement of a paired Joy-Con 2 / Pro Controller 2. | `switch2` |
| **Android TV Remote** | `android-tv` | Send directional, media, and navigation commands to Android TV and Google TV devices over local network. | `androidtv` |
| **Samsung Smart TV (Tizen)** | `samsung-tv` | Samsung TVs from 2016: every key with holds, apps, text, power state, Wake-on-LAN. Allow-on-TV pairing. | `samsungtv` |
| **LG webOS TV** | `lg-webos` | LG TVs from 2014: buttons, volume, inputs, apps, text, power, Wake-on-LAN. Accept-on-TV pairing. | `lgwebos` |
| **Sony Bravia TV** | `sony-bravia` | Sony Bravia TVs from 2013 over IP control with a pre-shared key: IRCC keys, power, inputs, volume. | `sonybravia` |
| **Wake-on-LAN** | `wol` | Wake PCs, NAS, TVs and consoles: a Wake-on-LAN device, or `PLUGIN:wol:<MAC>` on any device. Find MAC from the IP. | `wol` |

---

## Repository Structure

```text
.
├── repository.json              # Marketplace catalog (written by the release workflow)
├── roku-ecp/                    # Roku Wi-Fi ECP plugin source & manifest
├── ps5-wake/                    # PS5 Bluetooth Wake plugin source & manifest
├── switch2-wake/                # Switch 2 Bluetooth LE Wake plugin source, manifest & self-check
├── android-tv/                  # Android TV network controller source, manifest & tests
├── samsung-tv/ lg-webos/ sony-bravia/  # TV plugins, each with a test against a fake TV
├── wol/                         # Wake-on-LAN plugin
├── common/                      # Shared headers: plugin.h, wol.h, tls.h (Hub BearSSL), ws.h
├── template/                    # A plugin to copy (built and tested by CI, never released)
├── build_plugins.sh             # Builds every plugin for the Hub and packages dist/*.tar.gz
├── scripts/
│   ├── gen_repository.py        # repository.json from the manifests + archive SHA-256s
│   ├── bearssl_exports.txt      # Names the Hub's libbearssl.so.0 exports (copy of the firmware's)
│   ├── fake_ws.py               # Fake WebSocket/TLS device and plugin helpers for the tests
│   └── run_host_tests.sh        # Host tests (every plugin against a fake device)
└── .github/workflows/
    ├── ci.yml                   # Build + host tests on every push and pull request
    └── release.yml              # Releases new plugin versions, updates repository.json
```

## Building, Testing & Releasing

**Locally** (WSL/Linux, with a `harmony-hub-control` checkout next to this repo for the MIPS
toolchain and BearSSL, or `TOOLCHAIN_BIN` and `BEARSSL_DIR` set):

```bash
./build_plugins.sh                                   # dist/<id>-plugin.tar.gz + repository.json
BEARSSL_DIR=../harmony-hub-control/build/tmp/bearssl scripts/run_host_tests.sh
```

**CI** (`ci.yml`) runs the same on every push and pull request. It fetches the Bootlin MIPS
uClibc toolchain (pinned by SHA-256) and BearSSL (pinned commit), and uploads the archives as
the `plugin-archives` artifact.

**Releases** (`release.yml`): bump `version` in a plugin's `manifest.json` and push to `main`
(or run the workflow by hand). For each plugin version without a release, the workflow
creates the release `<id>-v<version>` with the CI-built `<id>-plugin.tar.gz`. It then rewrites
`repository.json` from the published assets (download URL and SHA-256, which the Hub checks
before it installs) and commits it to `main`. A released version is never rebuilt: change the
version to publish a change.

---

## How to Create a Plugin

Start from [`template/`](template/README.md): a working plugin on the shared headers in
`common/`, with its test. See the full [**HarmonyNG Plugin Architecture & Developer Guide**](https://github.com/iranl/harmony-ng/blob/main/docs/PLUGINS.md).

### 1. Requirements
- Atheros AR9344 MIPS 24Kc (big-endian) compatibility.
- Light footprint (< 500 KB unpacked).
- Zero flash wear (sockets and runtime files must use `/tmp/plugins/`).
- The manifest `entrypoint` is the binary itself. The Hub starts it, keeps it in the foreground and stops it (SIGTERM, then SIGKILL after 1 s): never daemonize, write no pid file. The socket path is `argv[1]` and `HARMONY_SOCKET`.
- Listen on UNIX domain datagram socket: `/tmp/plugins/<plugin_id>.sock`.
