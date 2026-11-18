#!/bin/sh
set -e

HOST_MAC="$1"
CTRL_MAC="$2"

[ -z "$HOST_MAC" ] && exit 1

if [ -z "$CTRL_MAC" ]; then
    echo "bt_page $HOST_MAC" | nc -u -w1 /tmp/hub_btstack.sock 2>/dev/null || true
    exit 0
fi

# CSR PSKey &0x0001 mapping: m[3], m[4]m[5], m[2], m[0]m[1]
PSR_VAL=$(echo "$CTRL_MAC" | tr 'A-Z' 'a-z' | sed 's/\([0-9a-f]\{2\}\):\([0-9a-f]\{2\}\):\([0-9a-f]\{2\}\):\([0-9a-f]\{2\}\):\([0-9a-f]\{2\}\):\([0-9a-f]\{2\}\)/0x\4 0x\5\6 0x\3 0x\1\2/')

PSR_BASE="/tmp/pskeys.psr"
[ ! -f "$PSR_BASE" ] && PSR_BASE="/lib/firmware/csr8811-a08.psr"
[ ! -f "$PSR_BASE" ] && PSR_BASE="/lib/firmware/csr8811-a06.psr"

# 1. Prepare spoof PSR
grep -v '&0x0001' "$PSR_BASE" > /tmp/ps5_spoof.psr
echo "&0x0001 = $PSR_VAL" >> /tmp/ps5_spoof.psr

# 2. Acquire lock, pause hub_btstack supervisor, release UART
touch /tmp/hub_btstack.lock
killall -9 hub_btstack hciattach 2>/dev/null || true
sleep 1

# 3. Load spoofed MAC into CSR8811 via BCSP
/usr/sbin/bccmd -t bcsp -d /dev/ttyS0 -b 115200 psload -r /tmp/ps5_spoof.psr
sleep 1

# 4. Attach kernel BCSP line discipline
/usr/sbin/hciattach ttyS0 bcsp 2>/dev/null &
sleep 2
/usr/sbin/hciconfig hci0 up 2>/dev/null || true
/usr/sbin/hciconfig hci0 sspmode 1 2>/dev/null || true

# 5. Page PS5 using bundled ps5_page tool
BIN_DIR=$(cd "$(dirname "$0")" && pwd)
"$BIN_DIR/ps5_page" "$HOST_MAC" 2 10 2>&1 || true
sleep 1

# 6. Restore genuine Hub BD_ADDR
killall -9 hciattach 2>/dev/null || true
sleep 1
/usr/sbin/bccmd -t bcsp -d /dev/ttyS0 -b 115200 psload -r "$PSR_BASE"
sleep 1
/usr/sbin/hciattach ttyS0 bcsp 2>/dev/null &
sleep 2
/usr/sbin/hciconfig hci0 up 2>/dev/null || true
/usr/sbin/hciconfig hci0 sspmode 1 2>/dev/null || true

# 7. Release lock and restart hub_btstack
rm -f /tmp/hub_btstack.lock
/data/harmony/bin/hub_btstack &
