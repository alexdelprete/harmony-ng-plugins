# Nintendo Switch 2 Bluetooth Wake Plugin for Harmony-NG

Native C daemon that wakes a Nintendo Switch 2 from sleep over Bluetooth LE, the same way a paired Joy-Con 2 or Pro Controller 2 does.

## Features
- **BLE wake advertisement**: The Hub sends the 31-byte non-connectable advertisement a sleeping Switch 2 listens for (Nintendo manufacturer data, controller product id, wake flag `0x81`, the console's address), from the paired controller's address.
- **Find Switch 2**: The Hub fills in the settings itself. It listens while you press a button on the paired controller, and reads the console's address from the controller's own wake advertisement.
- **No radio restart**: `hub_btstack` sends the advertisement. The Hub's Bluetooth remotes and hosts stay connected. One wake takes about 2 seconds.
- **No pairing or keys**: The console checks only that the advertisement comes from a controller it is bonded with.

## Prerequisites
- A HarmonyNG core with the plugin Bluetooth helpers `adv ... addr=` and `adv_listen`, and pairing without a host (`"type": "listen"`). On an older core, Wake and Find do nothing; the log names the missing command.
- A Joy-Con 2 or Pro Controller 2 already paired with the Switch 2. The Hub sends the wake as that controller.
- The Switch 2 in sleep mode (not fully powered off).

## Setup: Find Switch 2
1. Add a device of type **Nintendo Switch 2 (BT Wake)**.
2. Put the Switch 2 to sleep.
3. Press **Find Switch 2**. Within 30 seconds, press a button on the paired Joy-Con 2 or Pro Controller 2. The Switch 2 wakes up from that press as usual.
4. The Hub fills in `switch_mac`, `controller_mac` and `controller_pid`. Save the device.

### Manual method
If Find hears nothing (for example, the controller is out of the Hub's range), read the values with a BLE scanner app on a phone, such as nRF Connect:
1. Put the Switch 2 to sleep, then press a button on the paired controller. It now sends its wake advertisements.
2. In the scanner, find the device whose manufacturer data starts with `53 05 01 00 03 7E 05`.
   - The advertiser address is `controller_mac`.
   - Bytes 17 to 22 of the raw advertisement are the Switch 2 address in reverse order. Reverse them to get `switch_mac`. For example, `97 1F 99 55 E2 98` is `98:E2:55:99:1F:97`.
   - Bytes 12 and 13 are the product id in little-endian order (`66 20` is `2066`). Use them as `controller_pid`.

## Configuration Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `switch_mac` | String | **Yes** | Bluetooth MAC address of the Switch 2 (format: `AA:BB:CC:DD:EE:FF`). |
| `controller_mac` | String | **Yes** | Bluetooth MAC address of the paired controller. |
| `controller_pid` | String | No | `2066` (Joy-Con 2 L, default), `2067` (Joy-Con 2 R) or `2069` (Pro Controller 2). |

## Available Commands
- `PowerOn`: Sends the wake advertisement for 2 seconds.
- `Wake`: Alias for `PowerOn`.

A button wakes the console once, on press (repeats and the release are ignored). Other commands are ignored.

## Limitations
- The advertisement goes out with a random address type (TxAdd = 1), while a real controller uses its public address. If the console ignores the wake although a scanner shows the correct packet, the address type is the likely cause. The log of a Find records the address type the real controller used.
- Bluetooth 4.0 (the Hub's CSR8811) sends non-connectable advertisements at most every 100 ms, so a 2-second wake carries about 20 packets per channel.

## Attribution
This plugin is based on reverse-engineering work from the open-source community:
- **[alexvnesta/switch2controller](https://github.com/alexvnesta/switch2controller)**: Switch 2 BLE wake-packet research, payload layout and the method to read the controller and console addresses.
- **[ndeadly](https://github.com/ndeadly)**: Switch 2 controller protocol notes that the research above cites.

The plugin is an independent implementation written from the documented packet format. It does not copy code from these projects.
