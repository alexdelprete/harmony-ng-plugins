# PlayStation 5 / PlayStation 4 Bluetooth Wake Plugin for Harmony-NG

Native C daemon for waking Sony PlayStation 5 and PlayStation 4 consoles from both Rest Mode and complete cold power-off over Bluetooth.

## Features
- **Bluetooth HCI Wake**: Emulates an official Sony controller connection request to wake the console wirelessly.
- **Controller BD_ADDR Spoofing**: Spoofing of a paired controller's Bluetooth MAC address for instant wake acceptance.
- **UDP Broadcast Fallback**: Optional Sony remote-play wake packet transmission over local network.

## Deep Standby / Cold Power-off Wake
While UDP LAN wake only functions when the console is in Rest Mode, Bluetooth baseband paging allows waking the PlayStation 5 even from complete cold power-off (shutdown with AC power connected).

To achieve this, the plugin leverages BCSP persistent store reload on the Hub's CSR8811 transceiver to spoof the paired DualSense controller's BD_ADDR and sends an `HCI_Create_Connection` request with R2 page scan repetition mode (`0x02`) and mandatory role switch (`0x01`).

For the complete reverse-engineering analysis, sequence diagrams, and hardware implementation details, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Attribution
This plugin and its MAC address extraction methods are based on research and reverse-engineering work from the open-source community:
- **[FreeTHX/pywakepsXonbt](https://github.com/FreeTHX/pywakepsXonbt)**: Python library for waking PS3, PS4, and PS5 over Bluetooth via HCI connection packets.
- **[chrippa/pywakeps4onbt](https://github.com/chrippa/pywakeps4onbt)**: Original PlayStation 4 Bluetooth wake implementation.
- **Home Assistant [wakepsx-on-bt](https://github.com/FreeTHX/wakepsx-on-bt)** community.

## How to Obtain Bluetooth Host & Controller MAC Addresses
Waking the console over Bluetooth requires:
1. The **PlayStation Bluetooth MAC address** (`host_mac`).
2. (Recommended) The **paired Controller Bluetooth MAC address** (`controller_mac`).

Sony controllers store both their own hardware MAC address and the paired console's Bluetooth MAC address in their internal USB HID feature reports. You can read both in seconds using the included standalone cross-platform Python tool.

### Running `get_ps_mac.py`
The extraction tool is completely self-contained with **zero dependencies** (no `pip install`, no `libusb`, no Zadig drivers required). It uses native OS APIs on Windows (`hid.dll`), Linux (`/dev/hidraw`), and macOS (`IOKit`).

1. Connect your official Sony controller (DualSense, DualShock 4, or DualShock 3) to your PC using a USB data cable.
2. Run the script:
   ```bash
   python tools/get_ps_mac.py
   ```
3. Output example:
   ```text
   ============================================================
     PlayStation Controller Bluetooth Address Extractor
   ============================================================
   [+] Found Sony Controller: DualSense (USB ID 054C:0CE6)
   [+] Controller BT Address (controller_mac): 00:1A:7D:DA:71:02
   [+] Paired Console BT Address (host_mac) : 90:CD:B6:42:38:1A
   ============================================================
   ```
4. Copy `host_mac` and `controller_mac` into the device settings in Harmony-NG.

## Configuration Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `host_mac` | String | **Yes** | Bluetooth MAC address of your PlayStation console (format: `AA:BB:CC:DD:EE:FF`). |
| `controller_mac` | String | **Yes** | Bluetooth MAC address of your paired DualSense/DualShock controller.
| `host_ip` | String | Optional | Console local IP address for secondary UDP wake broadcast. |

## Available Commands
- `PowerOn`: Sends the Bluetooth connection wake frame to the console.
- `Wake`: Alias for `PowerOn`.
