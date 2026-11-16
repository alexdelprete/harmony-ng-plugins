# Android TV / Google TV Remote (v2) Plugin for Harmony-NG

Native C daemon for local network control of Android TV, Google TV, NVIDIA Shield, and Chromecast with Google TV devices via the official **Google TV Remote Protocol v2** (TLS).

## Features
- **No ADB Required**: Uses Google's official encrypted protocol (TLS ports `6466` and `6467`), completely eliminating the need for ADB debugging authorization or ADB server background processes.
- **Direct Wire Protobufs**: Fast, binary Protocol Buffer command injection for ultra-responsive navigation.
- **Bi-Directional Pairing**: Secure TLS client certificate exchange with on-screen TV PIN verification.
- **Persistent Credentials**: Paired client certificate and authorization tokens are stored safely on the Hub in `/data/harmony/plugins/android-tv/`.

## Architecture & Ports
| Port | Protocol | Purpose |
|---|---|---|
| `6466` | TLS | Initial pairing handshake, OuterMessage exchange, and PIN verification. |
| `6467` | Authenticated TLS | Runtime command execution (`RemoteKeyInject`) and session heartbeats. |

## Pairing Procedure
1. Ensure your Android TV / Google TV is powered on and connected to the same Wi-Fi / local network.
2. In Harmony-NG Devices Studio, click **Add Device** -> Select **Android TV / Google TV (Plugin)**.
3. Enter your TV's local IP address (e.g. `192.168.1.60`).
4. In the device settings, click **Pair with TV**:
   - The plugin initiates the TLS handshake on port `6466`.
   - Your TV screen will display a **6-character PIN code** (e.g. `B83F1A`).
   - Enter the PIN code into the Harmony-NG prompt and confirm.
5. Once paired, the TV confirms the pairing request and all commands immediately start routing over TLS port `6467`.

## Available Commands
- **Navigation**: `Home`, `Back`, `Select` (D-Pad Center), `DirectionUp`, `DirectionDown`, `DirectionLeft`, `DirectionRight`
- **Volume**: `VolumeUp`, `VolumeDown`, `VolumeMute`
- **System & Media**: `PowerToggle`, `Play`, `Menu`, `Search`
