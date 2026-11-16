# Android TV / Google TV Remote (v2) Plugin for Harmony-NG

Native C daemon for local network control of Android TV, Google TV, NVIDIA Shield, and Chromecast with Google TV devices via the official **Google TV Remote Protocol v2** (TLS).

Designed for **100% protocol and feature parity** with the [Home Assistant `androidtv_remote` integration](https://github.com/home-assistant/core/tree/dev/homeassistant/components/androidtv_remote) and the [`androidtvremote2`](https://github.com/tronikos/androidtvremote2) protocol engine.

---

## Highlights

- **Zero ADB Dependency**: Connects directly to Google's official Android TV Remote Service on TLS ports `6466` and `6467`. No ADB debugging, no developer options, and no ADB server background processes.
- **Home Assistant Command Compatibility**: Full support for all Home Assistant `TvKeys` names, direction prefixes (`SHORT:`, `START_LONG:`, `END_LONG:`), text entry (`text:`), and app deep links (`app:`).
- **Direct Wire Protobufs**: Nanosecond C-level Protocol Buffer packet serialization for immediate tactile response on Harmony remotes.
- **Embedded BearSSL TLS**: Self-contained TLS 1.2 client engine with pre-generated RSA authentication certificates for fast pairing and seamless execution.
- **Bi-Directional Pairing**: Interactive pairing flow with on-screen 6-character PIN code exchange.

---

## Ports & Protocol

| Port | Transport | Purpose |
|---|---|---|
| `6466` | TLS (Polo protocol) | Initial pairing handshake, `OuterMessage` negotiation, and PIN verification. |
| `6467` | Authenticated TLS | Runtime command injection (`RemoteKeyInject`), text input (`RemoteImeBatchEdit`), app launches (`RemoteAppLinkLaunchRequest`), and session heartbeats. |

---

## Pairing Instructions

1. Ensure your Android TV / Google TV is powered on and connected to the same Wi-Fi / local network as your Harmony Hub.
2. In the Harmony-NG WebUI (**Devices Studio**), click **Add Device** and select **Android TV / Google TV (Plugin)**.
3. Enter your TV's local IP address (e.g. `192.168.1.60`).
4. Click **Pair with TV** (or send the `Pair` command):
   - The plugin initiates the pairing handshake on port `6466`.
   - Your TV screen displays a **6-character PIN code** (e.g. `B83F1A`).
   - Enter the PIN code when prompted in the WebUI.
5. Once verified, the credentials are saved on the Hub in `/data/harmony/plugins/android-tv/state.txt`. All future commands route through authenticated TLS port `6467`.

---

## Command Syntax & Home Assistant Parity

The plugin supports all command patterns supported by Home Assistant's `remote.send_command` and `media_player.play_media`:

### 1. Direction Prefixes (Press, Hold, Release)
Supports explicit direction modifiers matching Home Assistant's `remote.py`:
- `SHORT:<command>` — Standard momentary key tap (default).
- `START_LONG:<command>` — Key down / start hold (used for continuous scrolling, fast-forwarding, or turning on TV).
- `END_LONG:<command>` — Key up / release hold.

*Example*: `START_LONG:DPAD_RIGHT` followed by `END_LONG:DPAD_RIGHT`.

### 2. Text Input (`text:<string>`)
Send text directly into search boxes, text fields, or URL inputs on Android TV without typing letter by letter:
- `text:Stranger Things`
- `text:The Matrix`

Behind the scenes, this sends a Google TV `RemoteImeBatchEdit` message (Protobuf field 21), typing the entire string instantly.

### 3. App Launching & Deep Linking (`app:<link>` or `launch:<link>`)
Directly launch streaming applications, URLs, or specific app activities:

| Command | Action |
|---|---|
| `app:youtube` or `app:https://www.youtube.com` | Opens YouTube |
| `app:netflix` or `app:com.netflix.ninja` | Opens Netflix |
| `app:prime` or `app:com.amazon.amazonvideo.livingroom` | Opens Amazon Prime Video |
| `app:disney` or `app:com.disney.disneyplus` | Opens Disney+ |
| `app:spotify` or `app:com.spotify.tv.android` | Opens Spotify |
| `app:plex` or `app:com.plexapp.android` | Opens Plex |
| `app:kodi` or `app:org.xbmc.kodi` | Opens Kodi |
| `app:vlc` or `app:org.videolan.vlc` | Opens VLC |
| `app:twitch` or `app:tv.twitch.android.app` | Opens Twitch |
| `app:https://example.com/video` | Opens Web Browser / Deep link |

### 4. Sequential Channel Switching (`channel:<digits>`)
Emulates Home Assistant's `media_player.play_media(media_type="channel", media_id="104")`:
- `channel:104` — Automatically sends `1`, `0`, `4` keycodes sequentially with a 100ms inter-digit delay.

---

## Full Keycode Reference Table

All commands accept either standard friendly names, formal Android `TvKeys` / `KEYCODE_*` constants, or raw integer values:

### Navigation & D-Pad
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `Up`, `DirectionUp` | `DPAD_UP` | `KEYCODE_DPAD_UP` | `19` |
| `Down`, `DirectionDown` | `DPAD_DOWN` | `KEYCODE_DPAD_DOWN` | `20` |
| `Left`, `DirectionLeft` | `DPAD_LEFT` | `KEYCODE_DPAD_LEFT` | `21` |
| `Right`, `DirectionRight` | `DPAD_RIGHT` | `KEYCODE_DPAD_RIGHT` | `22` |
| `Select`, `OK`, `Enter` | `DPAD_CENTER`, `ENTER` | `KEYCODE_DPAD_CENTER` | `23` |
| `Back`, `Return` | `BACK` | `KEYCODE_BACK` | `4` |
| `Home` | `HOME` | `KEYCODE_HOME` | `3` |
| `Menu` | `MENU` | `KEYCODE_MENU` | `82` |
| `Settings` | `SETTINGS` | `KEYCODE_SETTINGS` | `176` |
| `Search` | `SEARCH` | `KEYCODE_SEARCH` | `84` |
| `Assist`, `VoiceAssist` | `ASSIST`, `VOICE_ASSIST` | `KEYCODE_ASSIST` | `219` |
| `Info` | `INFO` | `KEYCODE_INFO` | `165` |
| `Guide` | `GUIDE` | `KEYCODE_GUIDE` | `172` |
| `Captions`, `Subtitles` | `CAPTIONS` | `KEYCODE_CAPTIONS` | `175` |
| `Notifications` | `NOTIFICATION` | `KEYCODE_NOTIFICATION` | `83` |
| `AppSwitcher` | `APP_SWITCH` | `KEYCODE_APP_SWITCH` | `187` |
| `AllApps` | `ALL_APPS` | `KEYCODE_ALL_APPS` | `284` |

### Power
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `PowerToggle`, `Power` | `POWER` | `KEYCODE_POWER` | `26` |
| `PowerOn`, `Wakeup` | `WAKEUP` | `KEYCODE_WAKEUP` | `224` |
| `PowerOff`, `Sleep` | `SLEEP` | `KEYCODE_SLEEP` | `223` |
| `TvPower` | `TV_POWER` | `KEYCODE_TV_POWER` | `177` |

### Volume & Audio
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `VolumeUp` | `VOLUME_UP` | `KEYCODE_VOLUME_UP` | `24` |
| `VolumeDown` | `VOLUME_DOWN` | `KEYCODE_VOLUME_DOWN` | `25` |
| `VolumeMute`, `Mute` | `VOLUME_MUTE`, `MUTE` | `KEYCODE_VOLUME_MUTE` | `164` |
| `AudioTrack` | `MEDIA_AUDIO_TRACK` | `KEYCODE_MEDIA_AUDIO_TRACK` | `222` |

### Media Playback
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `Play` | `MEDIA_PLAY` | `KEYCODE_MEDIA_PLAY` | `126` |
| `Pause` | `MEDIA_PAUSE` | `KEYCODE_MEDIA_PAUSE` | `127` |
| `PlayPause` | `MEDIA_PLAY_PAUSE` | `KEYCODE_MEDIA_PLAY_PAUSE` | `85` |
| `Stop` | `MEDIA_STOP` | `KEYCODE_MEDIA_STOP` | `86` |
| `Next` | `MEDIA_NEXT` | `KEYCODE_MEDIA_NEXT` | `87` |
| `Previous`, `Prev` | `MEDIA_PREVIOUS` | `KEYCODE_MEDIA_PREVIOUS` | `88` |
| `FastForward`, `Fwd` | `MEDIA_FAST_FORWARD` | `KEYCODE_MEDIA_FAST_FORWARD` | `90` |
| `Rewind`, `Rev` | `MEDIA_REWIND` | `KEYCODE_MEDIA_REWIND` | `89` |
| `Record` | `MEDIA_RECORD` | `KEYCODE_MEDIA_RECORD` | `130` |
| `TopMenu` | `MEDIA_TOP_MENU` | `KEYCODE_MEDIA_TOP_MENU` | `226` |
| `SkipForward` | `MEDIA_SKIP_FORWARD` | `KEYCODE_MEDIA_SKIP_FORWARD` | `272` |
| `SkipBackward` | `MEDIA_SKIP_BACKWARD` | `KEYCODE_MEDIA_SKIP_BACKWARD` | `273` |

### Channels & Numbers
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `0` to `9` | `0` to `9` | `KEYCODE_0` to `KEYCODE_9` | `7` to `16` |
| `ChannelUp` | `CHANNEL_UP` | `KEYCODE_CHANNEL_UP` | `166` |
| `ChannelDown` | `CHANNEL_DOWN` | `KEYCODE_CHANNEL_DOWN` | `167` |
| `LastChannel`, `PrevChannel` | `LAST_CHANNEL` | `KEYCODE_LAST_CHANNEL` | `229` |

### Color Keys
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `Red` | `PROG_RED` | `KEYCODE_PROG_RED` | `183` |
| `Green` | `PROG_GREEN` | `KEYCODE_PROG_GREEN` | `184` |
| `Yellow` | `PROG_YELLOW` | `KEYCODE_PROG_YELLOW` | `185` |
| `Blue` | `PROG_BLUE` | `KEYCODE_PROG_BLUE` | `186` |

### TV Inputs
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `Input`, `TvInput` | `TV_INPUT` | `KEYCODE_TV_INPUT` | `178` |
| `HDMI 1` | `TV_INPUT_HDMI_1` | `KEYCODE_TV_INPUT_HDMI_1` | `243` |
| `HDMI 2` | `TV_INPUT_HDMI_2` | `KEYCODE_TV_INPUT_HDMI_2` | `244` |
| `HDMI 3` | `TV_INPUT_HDMI_3` | `KEYCODE_TV_INPUT_HDMI_3` | `245` |
| `HDMI 4` | `TV_INPUT_HDMI_4` | `KEYCODE_TV_INPUT_HDMI_4` | `246` |
| `Composite 1` | `TV_INPUT_COMPOSITE_1` | `KEYCODE_TV_INPUT_COMPOSITE_1` | `247` |
| `Component 1` | `TV_INPUT_COMPONENT_1` | `KEYCODE_TV_INPUT_COMPONENT_1` | `249` |

### Dedicated Hardware App Buttons
| Friendly Name | Home Assistant Name | Android Constant | Keycode |
|---|---|---|---|
| `Netflix` | `VIDEO_APP_1` | `KEYCODE_VIDEO_APP_1` | `289` |
| `YouTube` | `VIDEO_APP_2` | `KEYCODE_VIDEO_APP_2` | `290` |
| `PrimeVideo`, `Prime` | `VIDEO_APP_3` | `KEYCODE_VIDEO_APP_3` | `291` |
| `Disney`, `Disney+` | `VIDEO_APP_4` | `KEYCODE_VIDEO_APP_4` | `292` |
| `Button1` to `Button4` | `BUTTON_1` to `BUTTON_4` | `KEYCODE_BUTTON_1` to `4` | `188` to `191` |

---

## Troubleshooting

### Android TV Not Responding to Pairing
If your TV does not display the 6-character PIN prompt or refuses connections:
1. On your Android TV, navigate to:
   `Settings` > `Apps` > `See all apps` > `Show system apps`.
2. Locate **Android TV Remote Service**.
3. Select **Clear Data / Clear Storage** and restart your TV.
4. Retry the pairing flow in Harmony-NG.

### Firewall & VLAN Configuration
Ensure local network firewalls permit traffic between your Harmony Hub and Android TV on:
- TCP Port `6466` (TLS Pairing)
- TCP Port `6467` (TLS Commands)
