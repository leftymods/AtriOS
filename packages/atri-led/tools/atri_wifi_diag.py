#!/usr/bin/env python3
"""
atri_wifi_diag.py — AtriStation RTL8822CS Wi-Fi & Bluetooth Diagnostic Tool (Python CLI)

Comprehensive diagnostic tool for Realtek RTL8822CS on Yandex Station Max / AtriStation:
- SDIO bus & clock detection (WL_REG_ON / GPIOX_8, 32.768kHz LPO clock)
- Kernel rtw88 driver & firmware probe
- eFuse validation (detects unprogrammed 0xFF blank OTP efuse)
- Virtual eFuse injection (/lib/firmware/rtw88/rtl8822cs_efuse.bin)
- Active Wi-Fi scan benchmark (latency in ms, discovered networks, RSSI)
- Bluetooth UART_A (/dev/ttyAML1), serdev, BT_EN (GPIOX_19), BT_HOST_WAKE (GPIOX_21)
- HCI device inquiry & status

Usage:
  python3 atri_wifi_diag.py [--all] [--wifi] [--bt] [--scan] [--efuse] [--inject-efuse] [--json]
"""

import os
import sys
import time
import subprocess
import argparse
import json
import glob
import re

VERSION = "1.2.0"

# ANSI colors
CLR_RESET = "\033[0m"
CLR_BOLD = "\033[1m"
CLR_RED = "\033[31m"
CLR_GREEN = "\033[32m"
CLR_YELLOW = "\033[33m"
CLR_CYAN = "\033[36m"
CLR_WHITE = "\033[37m"

def pass_msg(msg):
    print(f"  [{CLR_GREEN}PASS{CLR_RESET}] {msg}")

def fail_msg(msg):
    print(f"  [{CLR_RED}FAIL{CLR_RESET}] {msg}")

def warn_msg(msg):
    print(f"  [{CLR_YELLOW}WARN{CLR_RESET}] {msg}")

def info_msg(msg):
    print(f"  [{CLR_CYAN}INFO{CLR_RESET}] {msg}")

def sec_msg(title):
    print(f"\n{CLR_BOLD}{CLR_WHITE}=== {title} ==={CLR_RESET}")

def run_cmd(cmd):
    try:
        res = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
        return res.returncode, res.stdout, res.stderr
    except Exception as e:
        return -1, "", str(e)

def read_file_safe(path):
    try:
        with open(path, "r", errors="ignore") as f:
            return f.read().strip()
    except Exception:
        return None

# =========================================================================
# 1. SDIO Hardware Probe
# =========================================================================

def probe_sdio():
    sec_msg("1. Realtek RTL8822CS SDIO Bus & Hardware Probe")
    res = {"sdio_present": False, "vendor": "", "device": "", "clock_mhz": 0, "pass": False}

    sdio_devs = glob.glob("/sys/bus/sdio/devices/*")
    for d in sdio_devs:
        v = read_file_safe(os.path.join(d, "vendor"))
        dev = read_file_safe(os.path.join(d, "device"))
        if v and dev:
            res["sdio_present"] = True
            res["vendor"] = v
            res["device"] = dev
            pass_msg(f"SDIO device detected: {d} (vendor: {v}, device: {dev})")

            if v.lower() == "0x024c":
                chip = "Realtek RTL8822CS"
                if dev.lower() == "0xa822":
                    chip = "Realtek RTL8822CS (ITON RW8822-50B1 / Station Max)"
                elif dev.lower() == "0xc822":
                    chip = "Realtek RTL8822CS (Standard SDIO)"
                pass_msg(f"Chipset Model: {CLR_BOLD}{chip}{CLR_RESET}")
                res["pass"] = True
            else:
                warn_msg(f"Device vendor {v} != 0x024c (Realtek)")

            # Check clock
            clk = read_file_safe(os.path.join(d, "../../clock"))
            if clk and clk.isdigit():
                mhz = int(clk) // 1000000
                res["clock_mhz"] = mhz
                if mhz >= 50:
                    pass_msg(f"SDIO Bus Clock: {mhz} MHz (High-Speed / SDR50 OK)")
                else:
                    warn_msg(f"SDIO Bus Clock: {mhz} MHz (Low / Default Speed mode)")
            break

    if not res["sdio_present"]:
        fail_msg("No SDIO devices detected under /sys/bus/sdio/devices/")
        info_msg("Verify WL_REG_ON (GPIOX_8 active-high) and power sequencing")

    return res

# =========================================================================
# 2. Driver & eFuse Verification
# =========================================================================

def probe_driver_efuse():
    sec_msg("2. rtw88 Kernel Driver & eFuse Verification")
    res = {"modules_loaded": False, "fw_present": False, "efuse_bin_present": False, "efuse_valid": False}

    # Kernel modules
    modules_out = read_file_safe("/proc/modules") or ""
    if "rtw88_8822cs" in modules_out or "rtw88_core" in modules_out:
        res["modules_loaded"] = True
        pass_msg("Kernel modules loaded: rtw88_8822cs / rtw88_core")
    else:
        fail_msg("Kernel module rtw88_8822cs not loaded! (Run: modprobe rtw88_8822cs)")

    # Firmware files
    fw_wifi = "/lib/firmware/rtw88/rtw8822c_fw.bin"
    fw_efuse = "/lib/firmware/rtw88/rtl8822cs_efuse.bin"

    if os.path.isfile(fw_wifi):
        res["fw_present"] = True
        pass_msg(f"WiFi firmware present: {fw_wifi} ({os.path.getsize(fw_wifi)} bytes)")
    else:
        fail_msg(f"WiFi firmware missing: {fw_wifi}")

    if os.path.isfile(fw_efuse):
        res["efuse_bin_present"] = True
        pass_msg(f"Virtual physical eFuse present: {fw_efuse} ({os.path.getsize(fw_efuse)} bytes)")
    else:
        warn_msg(f"Virtual eFuse binary missing: {fw_efuse}")
        info_msg("Station Max hardware OTP eFuse is blank (0xFF)! Run '--inject-efuse' to install")

    # Inspect debugfs efuse map if available
    efuse_maps = glob.glob("/sys/kernel/debug/ieee80211/phy*/rtw88/efuse_map")
    if efuse_maps:
        try:
            with open(efuse_maps[0], "rb") as f:
                data = f.read(512)
            if len(data) >= 2:
                rtl_id = data[0] | (data[1] << 8)
                if data[0] == 0xff and data[1] == 0xff:
                    fail_msg("debugfs efuse_map contains blank 0xFF! RF synth will not lock on frequency")
                else:
                    xtal = data[0xB9] if len(data) > 0xB9 else 0
                    rfe = data[0xCA] if len(data) > 0xCA else 0
                    pass_msg(f"eFuse Logical Map valid (RTL_ID: 0x{rtl_id:04X}, CrystalCap: 0x{xtal:02X}, RFE: 0x{rfe:02X})")
                    res["efuse_valid"] = True
        except Exception as e:
            info_msg(f"Cannot read debugfs efuse_map: {e}")
    else:
        info_msg("debugfs rtw88 interface not mounted (CONFIG_RTW88_DEBUGFS=y)")

    # Check dmesg for rtw88 errors
    ret, dmesg_txt, _ = run_cmd("dmesg 2>/dev/null")
    if "failed to read efuse" in dmesg_txt:
        fail_msg("eFuse error found in dmesg: 'failed to read efuse' (hardware OTP blank)")
    elif "unsupported rfe_option" in dmesg_txt:
        fail_msg("RFE option error found in dmesg: 'unsupported rfe_option' (RF antenna switch unrouted)")
    elif "unsupported rf path" in dmesg_txt:
        fail_msg("RF path error found in dmesg: 'unsupported rf path'")
    else:
        pass_msg("No eFuse or RF path errors detected in kernel ring buffer")

    return res

# =========================================================================
# 3. Bluetooth Hardware & UART_A
# =========================================================================

def probe_bluetooth():
    sec_msg("3. Realtek RTL8822CS Bluetooth on UART_A")
    res = {"uart_present": False, "serdev_bound": False, "hci_up": False, "devices_found": 0}

    # Serial port
    if os.path.exists("/dev/ttyAML1"):
        res["uart_present"] = True
        pass_msg("Mainline UART_A port present: /dev/ttyAML1 (matches vendor /dev/ttyS1)")
    elif os.path.exists("/dev/ttyS1"):
        res["uart_present"] = True
        pass_msg("UART port present: /dev/ttyS1")
    else:
        fail_msg("UART_A port /dev/ttyAML1 not found! Check &uart_A in DTS")

    # Serdev binding
    serdevs = glob.glob("/sys/bus/serial/devices/*/of_node/compatible")
    for s in serdevs:
        c = read_file_safe(s) or ""
        if "rtl8822cs-bt" in c or "realtek" in c:
            res["serdev_bound"] = True
            dev_name = os.path.basename(os.path.dirname(os.path.dirname(s)))
            pass_msg(f"Serdev Bluetooth driver bound: {dev_name} ({c})")
            break

    if not res["serdev_bound"]:
        warn_msg("Serdev Bluetooth child not bound under /sys/bus/serial/devices/")

    # Firmware and config
    bt_fw = "/lib/firmware/rtl_bt/rtl8822cs_fw.bin"
    bt_cfg = "/lib/firmware/rtl_bt/rtl8822cs_config.bin"

    if os.path.isfile(bt_fw):
        pass_msg(f"Bluetooth firmware present: {bt_fw} ({os.path.getsize(bt_fw)} bytes)")
    else:
        fail_msg(f"Bluetooth firmware missing: {bt_fw}")

    if os.path.isfile(bt_cfg):
        pass_msg(f"Bluetooth vendor config present: {bt_cfg} ({os.path.getsize(bt_cfg)} bytes)")
    else:
        fail_msg(f"Bluetooth config missing: {bt_cfg}")

    # HCI interface
    hci_addr = read_file_safe("/sys/class/bluetooth/hci0/address")
    if hci_addr:
        res["hci_up"] = True
        pass_msg(f"HCI Interface UP: hci0 (BD_ADDR: {CLR_BOLD}{hci_addr}{CLR_RESET})")

        # Quick inquiry
        ret, out, _ = run_cmd("hcitool inq 2>/dev/null | grep -E '^[0-9A-Fa-f]{2}:'")
        if out.strip():
            lines = [l.strip() for l in out.strip().splitlines()]
            res["devices_found"] = len(lines)
            for l in lines:
                info_msg(f"Discovered BT device: {l}")
            pass_msg(f"Bluetooth RF RX/TX confirmed: {len(lines)} devices in range")
        else:
            info_msg("HCI Inquiry finished (0 devices nearby or scanning disabled)")
    else:
        warn_msg("hci0 not active. (Try: hciconfig hci0 up or systemctl restart bluetooth)")

    return res

# =========================================================================
# 4. Active Wi-Fi Scan Benchmark
# =========================================================================

def probe_wifi_scan():
    sec_msg("4. Active Wi-Fi Scan & RF Front-End Benchmark")
    res = {"iface": "", "scan_time_ms": 0, "networks_found": 0, "networks": []}

    # Find wlan interface
    ifaces = glob.glob("/sys/class/net/wlan*")
    if not ifaces:
        fail_msg("No wireless network interface (wlan0) found!")
        return res

    ifname = os.path.basename(ifaces[0])
    res["iface"] = ifname
    info_msg(f"Triggering active scan on {CLR_BOLD}{ifname}{CLR_RESET}...")

    # Bring up interface
    run_cmd(f"ip link set {ifname} up 2>/dev/null")

    t0 = time.time()
    ret, out, err = run_cmd(f"iw dev {ifname} scan 2>/dev/null")
    t1 = time.time()
    elapsed_ms = int((t1 - t0) * 1000)
    res["scan_time_ms"] = elapsed_ms

    info_msg(f"Scan finished in {elapsed_ms} ms ({elapsed_ms/1000:.2f} s)")

    if elapsed_ms > 15000:
        warn_msg("Scan took > 15 seconds! High scan latency indicates missing RF calibration or timeouts")
    else:
        pass_msg(f"Scan latency benchmark: FAST ({elapsed_ms} ms)")

    # Parse BSS entries
    networks = []
    current_bss = {}
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("BSS "):
            if current_bss.get("bssid"):
                networks.append(current_bss)
            m = re.match(r"BSS ([0-9a-fA-F:]{17})", line)
            current_bss = {"bssid": m.group(1) if m else "", "ssid": "<Hidden>", "freq": 0, "channel": 0, "signal": -100, "security": "Open"}
        elif line.startswith("SSID:"):
            current_bss["ssid"] = line[5:].strip() or "<Hidden>"
        elif line.startswith("freq:"):
            try:
                f = int(line[5:].strip())
                current_bss["freq"] = f
                if 2412 <= f <= 2484:
                    current_bss["channel"] = 14 if f == 2484 else (f - 2407) // 5
                elif 5180 <= f <= 5825:
                    current_bss["channel"] = (f - 5000) // 5
            except ValueError:
                pass
        elif line.startswith("signal:"):
            try:
                current_bss["signal"] = int(float(line[7:].split()[0]))
            except (ValueError, IndexError):
                pass
        elif "WPA3" in line:
            current_bss["security"] = "WPA3"
        elif "RSN" in line and current_bss["security"] == "Open":
            current_bss["security"] = "WPA2"
        elif "WPA" in line and current_bss["security"] == "Open":
            current_bss["security"] = "WPA"

    if current_bss.get("bssid"):
        networks.append(current_bss)

    res["networks_found"] = len(networks)
    res["networks"] = networks

    if networks:
        pass_msg(f"Discovered {len(networks)} Wi-Fi networks in range:")
        print(f"\n  {CLR_BOLD}{'CH':<4}  {'SSID':<24}  {'BSSID':<17}  {'FREQ':<5}  {'SIGNAL':<10}  {'SECURITY':<8}{CLR_RESET}")
        print("  " + "-" * 76)
        for b in networks[:20]:
            sig = b['signal']
            sig_str = f"{sig}dBm"
            if sig > -60:
                bar = f"{CLR_GREEN}[====]{CLR_RESET}"
            elif sig > -75:
                bar = f"{CLR_YELLOW}[=== ]{CLR_RESET}"
            else:
                bar = f"{CLR_RED}[==  ]{CLR_RESET}"
            print(f"  {b['channel']:<4}  {b['ssid'][:24]:<24}  {b['bssid']:<17}  {b['freq']}M  {sig_str:<6} {bar}  {b['security']:<8}")
        if len(networks) > 20:
            print(f"  ... and {len(networks) - 20} more networks.")
    else:
        fail_msg("Zero (0) Wi-Fi networks discovered!")
        info_msg("Why RTL8822CS sees 0 networks:")
        info_msg(" 1. Hardware eFuse is blank OTP (0xFF) -> crystal cap uncalibrated -> frequency offset")
        info_msg(" 2. RFE option unconfigured -> internal antenna switch disconnected from LNA")
        info_msg(" 3. Fix: Ensure /lib/firmware/rtw88/rtl8822cs_efuse.bin is installed")

    return res

# =========================================================================
# 5. eFuse Injection
# =========================================================================

def inject_efuse():
    sec_msg("Generating Calibrated RTL8822CS Virtual eFuse Binary")
    target_dir = "/lib/firmware/rtw88"
    target_path = os.path.join(target_dir, "rtl8822cs_efuse.bin")

    phy_efuse = bytearray(b'\xff' * 512)
    # Physical blocks:
    # blk 0: RTL_ID 0x8129
    # blk 23: Channel plan 0x7f, xtal_k 0x3f
    # blk 24: RF board opt 0x20 (BT coex)
    # blk 25: RFE opt 0x00, country EU
    # blk 26: Thermal meter 0x33, 0x33
    # blk 27: RX gain gaps
    # blk 45: SDIO MAC address
    patch_bytes = bytes([
        0x03, 0x29, 0x81, 0x7e, 0x7f, 0x3f, 0x80, 0x20,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x8a, 0x00, 0x00, 0x00, 0x45, 0x55, 0x8c, 0x33,
        0x33, 0x00, 0x00, 0x00, 0x00, 0x8e, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x0f, 0x68, 0x02, 0x1a,
        0x2b, 0x7c, 0x45, 0x9a
    ])
    phy_efuse[:len(patch_bytes)] = patch_bytes

    try:
        os.makedirs(target_dir, exist_ok=True)
        with open(target_path, "wb") as f:
            f.write(phy_efuse)
        pass_msg(f"Wrote {target_path} (512 bytes)")
        info_msg("Calibration values installed:")
        info_msg("  - Chip ID:     0x8129 (RTL8822C)")
        info_msg("  - Crystal Cap: 0x3F (40 MHz baseband ref)")
        info_msg("  - RFE Option:  0x00 (2T2R internal switch)")
        info_msg("  - BT Coex:     0x20 (Enabled)")
        info_msg("  - Regulatory:  EU (All 2.4G & 5G channels)")
        info_msg("Reload driver with: rmmod rtw88_8822cs && modprobe rtw88_8822cs")
    except Exception as e:
        fail_msg(f"Cannot write {target_path}: {e} (need sudo/root)")

# =========================================================================
# Main CLI
# =========================================================================

def main():
    parser = argparse.ArgumentParser(description=f"AtriStation RTL8822CS Diagnostic Tool v{VERSION}")
    parser.add_argument("-a", "--all", action="store_true", help="Run full diagnostic suite (default)")
    parser.add_argument("-w", "--wifi", action="store_true", help="Run Wi-Fi SDIO, driver, eFuse, and scan diagnostics")
    parser.add_argument("-b", "--bt", action="store_true", help="Run Bluetooth UART_A, serdev, and HCI diagnostics")
    parser.add_argument("-s", "--scan", action="store_true", help="Run active Wi-Fi scan benchmark")
    parser.add_argument("-e", "--efuse", action="store_true", help="Run eFuse validation")
    parser.add_argument("--inject-efuse", action="store_true", help="Write calibrated rtl8822cs_efuse.bin to /lib/firmware/rtw88/")
    parser.add_argument("-j", "--json", action="store_true", help="Output results in JSON format")
    args = parser.parse_args()

    if args.inject_efuse:
        inject_efuse()
        return

    run_all = args.all or (not args.wifi and not args.bt and not args.scan and not args.efuse)

    report = {"version": VERSION, "timestamp": time.time()}

    if not args.json:
        print(f"{CLR_BOLD}{CLR_CYAN}==========================================================={CLR_RESET}")
        print(f"{CLR_BOLD}{CLR_CYAN}  AtriStation RTL8822CS Hardware & Diagnostic Suite v{VERSION}{CLR_RESET}")
        print(f"{CLR_BOLD}{CLR_CYAN}==========================================================={CLR_RESET}")

    if run_all or args.wifi or args.efuse:
        report["sdio"] = probe_sdio()
        report["driver_efuse"] = probe_driver_efuse()

    if run_all or args.bt:
        report["bluetooth"] = probe_bluetooth()

    if run_all or args.scan or args.wifi:
        report["scan"] = probe_wifi_scan()

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"\n{CLR_BOLD}==========================================================={CLR_RESET}")
        healthy = True
        if report.get("sdio") and not report["sdio"].get("pass", False):
            healthy = False
        if report.get("driver_efuse") and not report["driver_efuse"].get("modules_loaded", False):
            healthy = False
        if report.get("scan") and report["scan"].get("networks_found", 0) == 0:
            healthy = False

        if healthy:
            print(f"  {CLR_BOLD}{CLR_GREEN}OVERALL STATUS: HEALTHY{CLR_RESET}")
        else:
            print(f"  {CLR_BOLD}{CLR_RED}OVERALL STATUS: ATTENTION NEEDED{CLR_RESET}")
        print(f"{CLR_BOLD}==========================================================={CLR_RESET}\n")

if __name__ == "__main__":
    main()
