#!/usr/bin/env python3
"""switch2-wake against a fake hub_btstack socket: the wake command it sends, and Find (the
pairing without a host) from the controller's own wake advertisement to the status file.
Usage: test_fake_btstack.py <switch2_wake binary built with -DHUB_BTSTACK_SOCK=<dir>/bt.sock> <dir>"""
import json
import os
import socket
import subprocess
import sys
import time

binary, work = sys.argv[1], sys.argv[2]
os.makedirs(work, exist_ok=True)
bt_path = os.path.join(work, "bt.sock")
plug_path = os.path.join(work, "switch2-wake.sock")
status_path = os.path.join(work, "switch2-wake.status")
for p in (bt_path, status_path):
    if os.path.exists(p):
        os.unlink(p)

bt = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
bt.bind(bt_path)
bt.settimeout(3)
log = open(os.path.join(work, "plugin.log"), "w")
proc = subprocess.Popen([binary, plug_path], stdout=log, stderr=subprocess.STDOUT)
failed = 0


def check(cond, name):
    global failed
    print("  %s %s" % ("PASS" if cond else "FAIL", name))
    failed += 0 if cond else 1


def send(obj):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.sendto(json.dumps(obj).encode(), plug_path)
    s.close()


def bt_recv():
    try:
        return bt.recv(512).decode()
    except socket.timeout:
        return ""


def status():
    try:
        return json.load(open(status_path))
    except (OSError, ValueError):
        return {}


try:
    for _ in range(50):
        if os.path.exists(plug_path):
            break
        time.sleep(0.1)

    cfg = {"switch_mac": "98:E2:55:99:1F:97", "controller_mac": "11:22:33:44:55:66"}
    send({"command": "Wake", "action": "press", "config": cfg})
    cmd = bt_recv()
    check(cmd == "adv 0201061BFF53050100037E0566200001819" "71F9955E2980F00000000000000 2000 addr=11:22:33:44:55:66\n",
          "Wake sends the wake advertisement from the controller's address")
    send({"command": "Wake", "action": "release", "config": cfg})
    send({"command": "Wake", "action": "press", "config": {"switch_mac": "bad", "controller_mac": "11:22:33:44:55:66"}})
    check(bt_recv() == "", "a release and a bad address send nothing")

    send({"plugin_id": "switch2-wake", "command": "pair", "action": "start_pair", "timeout_s": 20})
    check(bt_recv() == "adv_listen switch2-wake 20 53050100037E05\n", "Find asks hub_btstack to listen")
    time.sleep(0.2)
    check(status().get("stage") == "listening", "status: listening")

    pairing_ad = "0201061BFF53050100037E05692000010097" "1F9955E2980F00000000000000"
    send({"command": "adv_report", "addr": "AA:BB:CC:DD:EE:01", "addr_type": 0, "data": pairing_ad})
    time.sleep(0.2)
    check(status().get("stage") == "listening", "a pairing advertisement (flag 0x00) is not taken")

    wake_ad = "0201061BFF53050100037E05692000018197" "1F9955E2980F00000000000000"
    send({"command": "adv_report", "addr": "AA:BB:CC:DD:EE:02", "addr_type": 0, "data": wake_ad})
    check(bt_recv() == "adv_listen switch2-wake 0\n", "the listen is stopped once found")
    st = status()
    check(st.get("ok") is True and st.get("stage") == "done", "status: done")
    check(st.get("config") == {"switch_mac": "98:E2:55:99:1F:97", "controller_mac": "AA:BB:CC:DD:EE:02",
                               "controller_pid": "2069"}, "learned config: console, controller, product id")
finally:
    proc.terminate()
    proc.wait(5)
    log.close()

if failed:
    print("--- plugin log ---")
    print(open(os.path.join(work, "plugin.log")).read())
    sys.exit(1)
print("[+] switch2-wake fake hub_btstack: all checks passed")
