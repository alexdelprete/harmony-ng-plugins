#!/usr/bin/env python3
"""PlayStation Controller Bluetooth Address Extractor for Harmony-NG.

Extracts the paired PlayStation console Bluetooth MAC address (host_mac)
and the controller Bluetooth MAC address (controller_mac) directly from
an official Sony DualSense, DualShock 4, or DualShock 3 controller over USB.

Works on Windows, Linux, and macOS without third-party dependencies (no pip install required).

Based on upstream research by:
- FreeTHX/pywakepsXonbt (https://github.com/FreeTHX/pywakepsXonbt)
- chrippa/pywakeps4onbt (https://github.com/chrippa/pywakeps4onbt)
"""

from __future__ import annotations

import ctypes
import os
import platform
import struct
import sys
from typing import Optional, Tuple

SONY_VID = 0x054C

CONTROLLER_PROFILES = {
    "dualsense": {
        "name": "PlayStation 5 DualSense",
        "pids": [0x0CE6, 0x0DF2],  # Standard & Edge
        "report_id": 0x09,
        "report_len": 20,
        "ctrl_mac_slice": slice(1, 7),
        "host_mac_slice": slice(10, 16),
        "reverse": True,
    },
    "dualshock4": {
        "name": "PlayStation 4 DualShock 4",
        "pids": [0x05C4, 0x09CC],
        "report_id": 0x12,
        "report_len": 16,
        "ctrl_mac_slice": slice(1, 7),
        "host_mac_slice": slice(10, 16),
        "reverse": True,
    },
    "dualshock3": {
        "name": "PlayStation 3 DualShock 3",
        "pids": [0x0268],
        "report_id": 0xF2,
        "report_len": 17,
        "ctrl_mac_slice": slice(4, 10),
        "host_mac_slice": None,  # DS3 host mac is on 0xF5
        "reverse": False,
    },
}

ALL_PIDS = [pid for prof in CONTROLLER_PROFILES.values() for pid in prof["pids"]]


def format_mac(raw_bytes: bytes, reverse: bool = True) -> str:
    b = list(raw_bytes)
    if reverse:
        b.reverse()
    return ":".join(f"{x:02X}" for x in b)


# ==============================================================================
# Windows Native Implementation (SetupAPI + hid.dll)
# ==============================================================================
def extract_windows() -> list[dict]:
    import ctypes.wintypes as wintypes

    kernel32 = ctypes.windll.kernel32
    setupapi = ctypes.windll.setupapi
    hid_dll = ctypes.windll.hid

    class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.DWORD),
            ("InterfaceClassGuid", ctypes.c_byte * 16),
            ("Flags", wintypes.DWORD),
            ("Reserved", ctypes.POINTER(wintypes.ULONG)),
        ]

    class HIDD_ATTRIBUTES(ctypes.Structure):
        _fields_ = [
            ("Size", wintypes.DWORD),
            ("VendorID", wintypes.USHORT),
            ("ProductID", wintypes.USHORT),
            ("VersionNumber", wintypes.USHORT),
        ]

    setupapi.SetupDiGetClassDevsW.restype = wintypes.HANDLE
    setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, wintypes.HWND, wintypes.DWORD]
    setupapi.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL
    setupapi.SetupDiEnumDeviceInterfaces.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
    setupapi.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOL
    setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    setupapi.SetupDiDestroyDeviceInfoList.restype = wintypes.BOOL
    setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]

    hid_dll.HidD_GetHidGuid.restype = None
    hid_dll.HidD_GetHidGuid.argtypes = [ctypes.c_void_p]
    hid_dll.HidD_GetAttributes.restype = wintypes.BOOL
    hid_dll.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
    hid_dll.HidD_GetFeature.restype = wintypes.BOOL
    hid_dll.HidD_GetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]

    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = [ctypes.c_wchar_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    hid_guid = (ctypes.c_byte * 16)()
    hid_dll.HidD_GetHidGuid(ctypes.byref(hid_guid))

    DIGCF_PRESENT = 0x00000002
    DIGCF_DEVICEINTERFACE = 0x00000010
    h_dev_info = setupapi.SetupDiGetClassDevsW(ctypes.byref(hid_guid), None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    if h_dev_info == wintypes.HANDLE(-1).value:
        return []

    results = []
    idx = 0
    try:
        while True:
            dev_if_data = SP_DEVICE_INTERFACE_DATA()
            dev_if_data.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)
            if not setupapi.SetupDiEnumDeviceInterfaces(h_dev_info, None, ctypes.byref(hid_guid), idx, ctypes.byref(dev_if_data)):
                break
            idx += 1

            req_sz = wintypes.DWORD()
            setupapi.SetupDiGetDeviceInterfaceDetailW(h_dev_info, ctypes.byref(dev_if_data), None, 0, ctypes.byref(req_sz), None)
            if req_sz.value == 0:
                continue

            buf = ctypes.create_string_buffer(req_sz.value)
            cb_size = 8 if struct.calcsize("P") == 8 else (4 + ctypes.sizeof(ctypes.c_wchar))
            struct.pack_into("I", buf, 0, cb_size)

            if not setupapi.SetupDiGetDeviceInterfaceDetailW(h_dev_info, ctypes.byref(dev_if_data), buf, req_sz.value, None, None):
                continue

            path_offset = 4
            device_path = ctypes.wstring_at(ctypes.addressof(buf) + path_offset)

            # Open device handle
            GENERIC_READ = 0x80000000
            GENERIC_WRITE = 0x40000000
            FILE_SHARE_READ = 0x00000001
            FILE_SHARE_WRITE = 0x00000002
            OPEN_EXISTING = 3
            h_dev = kernel32.CreateFileW(
                device_path,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                None,
                OPEN_EXISTING,
                0,
                None
            )
            if h_dev == wintypes.HANDLE(-1).value:
                # Retry with no generic access
                h_dev = kernel32.CreateFileW(device_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
                if h_dev == wintypes.HANDLE(-1).value:
                    continue

            try:
                attrs = HIDD_ATTRIBUTES()
                attrs.Size = ctypes.sizeof(HIDD_ATTRIBUTES)
                if hid_dll.HidD_GetAttributes(h_dev, ctypes.byref(attrs)):
                    if attrs.VendorID == SONY_VID and attrs.ProductID in ALL_PIDS:
                        for p_key, prof in CONTROLLER_PROFILES.items():
                            if attrs.ProductID in prof["pids"]:
                                rep_len = prof["report_len"]
                                feat_buf = ctypes.create_string_buffer(rep_len)
                                struct.pack_into("B", feat_buf, 0, prof["report_id"])
                                if hid_dll.HidD_GetFeature(h_dev, feat_buf, rep_len):
                                    data = feat_buf.raw
                                    ctrl_mac = format_mac(data[prof["ctrl_mac_slice"]], prof["reverse"])
                                    host_mac = None
                                    if prof["host_mac_slice"]:
                                        host_mac = format_mac(data[prof["host_mac_slice"]], prof["reverse"])
                                    elif p_key == "dualshock3":
                                        # Query 0xF5 for host mac
                                        ds3_buf = ctypes.create_string_buffer(8)
                                        struct.pack_into("B", ds3_buf, 0, 0xF5)
                                        if hid_dll.HidD_GetFeature(h_dev, ds3_buf, 8):
                                            host_mac = format_mac(ds3_buf.raw[2:8], False)

                                    results.append({
                                        "name": prof["name"],
                                        "vid": attrs.VendorID,
                                        "pid": attrs.ProductID,
                                        "controller_mac": ctrl_mac,
                                        "host_mac": host_mac,
                                    })
            finally:
                kernel32.CloseHandle(h_dev)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(h_dev_info)

    return results


# ==============================================================================
# Linux Native Implementation (/dev/hidraw ioctl)
# ==============================================================================
def extract_linux() -> list[dict]:
    import fcntl
    import glob

    HIDIOCGFEAT = lambda length: 0x80004807 | ((length & 0x1FFF) << 16)

    results = []
    for dev_path in sorted(glob.glob("/dev/hidraw*")):
        base = os.path.basename(dev_path)
        uevent_path = f"/sys/class/hidraw/{base}/device/uevent"
        if not os.path.isfile(uevent_path):
            continue

        vid, pid = 0, 0
        with open(uevent_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if line.startswith("HID_ID="):
                    parts = line.strip().split("=")[1].split(":")
                    if len(parts) >= 3:
                        vid = int(parts[1], 16)
                        pid = int(parts[2], 16)

        if vid != SONY_VID or pid not in ALL_PIDS:
            continue

        for p_key, prof in CONTROLLER_PROFILES.items():
            if pid in prof["pids"]:
                try:
                    fd = os.open(dev_path, os.O_RDWR | os.O_NONBLOCK)
                except OSError:
                    continue

                try:
                    rep_len = prof["report_len"]
                    buf = bytearray(rep_len)
                    buf[0] = prof["report_id"]
                    res = fcntl.ioctl(fd, HIDIOCGFEAT(rep_len), buf, True)
                    if res >= 0:
                        data = bytes(buf)
                        ctrl_mac = format_mac(data[prof["ctrl_mac_slice"]], prof["reverse"])
                        host_mac = None
                        if prof["host_mac_slice"]:
                            host_mac = format_mac(data[prof["host_mac_slice"]], prof["reverse"])
                        elif p_key == "dualshock3":
                            ds3_buf = bytearray(8)
                            ds3_buf[0] = 0xF5
                            if fcntl.ioctl(fd, HIDIOCGFEAT(8), ds3_buf, True) >= 0:
                                host_mac = format_mac(bytes(ds3_buf)[2:8], False)

                        results.append({
                            "name": prof["name"],
                            "vid": vid,
                            "pid": pid,
                            "controller_mac": ctrl_mac,
                            "host_mac": host_mac,
                        })
                except OSError:
                    pass
                finally:
                    os.close(fd)

    return results


# ==============================================================================
# Generic / macOS Fallback (pyusb / hidapi if present)
# ==============================================================================
def extract_fallback() -> list[dict]:
    results = []
    try:
        import hid
        for p_key, prof in CONTROLLER_PROFILES.items():
            for pid in prof["pids"]:
                for dev_info in hid.enumerate(SONY_VID, pid):
                    try:
                        h = hid.device()
                        h.open_path(dev_info["path"])
                        data = h.get_feature_report(prof["report_id"], prof["report_len"])
                        h.close()
                        if data and len(data) >= prof["report_len"]:
                            ctrl_mac = format_mac(bytes(data)[prof["ctrl_mac_slice"]], prof["reverse"])
                            host_mac = format_mac(bytes(data)[prof["host_mac_slice"]], prof["reverse"]) if prof["host_mac_slice"] else None
                            results.append({
                                "name": prof["name"],
                                "vid": SONY_VID,
                                "pid": pid,
                                "controller_mac": ctrl_mac,
                                "host_mac": host_mac,
                            })
                    except Exception:
                        pass
    except ImportError:
        pass
    return results


def main():
    print("=" * 64)
    print("  PlayStation Controller Bluetooth Address Extractor (Harmony-NG)")
    print("=" * 64)

    controllers = []
    if sys.platform == "win32":
        controllers = extract_windows()
    elif sys.platform.startswith("linux"):
        controllers = extract_linux()

    if not controllers:
        controllers = extract_fallback()

    if not controllers:
        print("[-] No connected Sony PlayStation controllers found over USB.")
        print("    Please connect a DualSense, DualShock 4, or DualShock 3 via USB cable.")
        if sys.platform.startswith("linux") and os.geteuid() != 0:
            print("    [!] Tip: On Linux, run with sudo to access /dev/hidraw devices:")
            print("        sudo python tools/get_ps_mac.py")
        sys.exit(1)

    for c in controllers:
        print(f"[+] Found Controller : {c['name']} (USB ID {c['vid']:04X}:{c['pid']:04X})")
        print(f"    - Controller MAC  (controller_mac): {c['controller_mac']}")
        if c.get("host_mac"):
            print(f"    - Paired Host MAC (host_mac)      : {c['host_mac']}")
        else:
            print("    - Paired Host MAC (host_mac)      : Not paired yet")
        print("-" * 64)

    print("\nCopy 'host_mac' and 'controller_mac' into Harmony-NG device settings.")


if __name__ == "__main__":
    main()
