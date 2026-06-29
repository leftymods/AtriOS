#!/usr/bin/env python3
"""
Generate a valid physical efuse map for rtl8822cs (8822C).
Output: 512-byte binary file suitable for file-based efuse loading.

Built-in defaults for AtriStation board where HW efuse is dead.

Physical efuse format (Realtek):
- Header identifies which logical block + which word pairs are stored
- 1-byte header: (blk_idx << 4) | word_en  (blk_idx 0-15)
- 2-byte header: (block[2:0]<<5|0x0f) (block[6:3]<<4|word_en) (blk_idx > 15)
- word_en: 4 bits, bit i = 1 means word pair i is NOT stored
- Each blk_idx covers 8 bytes of logical map (4 word pairs × 2 bytes)
- Physical data = header + stored word pairs (2 bytes each)
"""
import struct
import random
import sys

PHY_SIZE = 512
LOG_SIZE = 768


def build_header(blk_idx, word_en):
    """word_en: 1 bit per word pair, 1 = NOT stored, 0 = stored."""
    if blk_idx < 16:
        return bytes([(blk_idx << 4) | (word_en & 0xf)])
    else:
        block2_0 = blk_idx & 0x7
        block6_3 = (blk_idx >> 3) & 0xf
        hdr1 = (block2_0 << 5) | 0x0f
        hdr2 = (block6_3 << 4) | (word_en & 0xf)
        return bytes([hdr1, hdr2])


def encode_block(log, blk_idx):
    """Encode one logical 8-byte block into physical efuse format.
    Returns bytes (header + stored word pairs), or empty if nothing to store.
    """
    start = blk_idx * 8
    block = log[start:start + 8]
    word_en = 0
    data = b""
    for i in range(4):
        lo = i * 2
        word = block[lo:lo + 2]
        if word == b'\xff\xff':
            word_en |= (1 << i)  # not stored
        else:
            data += word
    if word_en == 0xf:
        return b""  # nothing to store
    hdr = build_header(blk_idx, word_en)
    return hdr + data


def main():
    # Start with all-0xff logical map (unprogrammed)
    log = bytearray(b'\xff' * LOG_SIZE)

    # === Fill in calibration values ===

    # RTL ID at offset 0 (little-endian 0x8129 for 8822C)
    struct.pack_into('<H', log, 0, 0x8129)

    # Channel plan (0xB8) + xtal_k / crystal_cap (0xB9)
    log[0xB8] = 0x7f
    log[0xB9] = 0x3f        # crystal_cap = 0x3f

    # RF board option (0xC1)
    log[0xC1] = 0x20        # BT coex enabled

    # RF feature option (0xC2)
    log[0xC2] = 0x00

    # BT setting (0xC3)
    log[0xC3] = 0x00

    # EEPROM version (0xC4) + customer ID (0xC5)
    log[0xC4] = 0x00
    log[0xC5] = 0x00

    # TX BB swing 2g (0xC6) + 5g (0xC7)
    log[0xC6] = 0x00
    log[0xC7] = 0x00

    # TX pwr calibrate rate (0xC8) + RF antenna option (0xC9)
    log[0xC8] = 0x00
    log[0xC9] = 0x00

    # RFE option (0xCA)
    log[0xCA] = 0x00

    # Country code (0xCB-0xCC)
    log[0xCB] = ord('E')
    log[0xCC] = ord('U')

    # Thermal meter (0xD0-0xD1)
    log[0xD0] = 0x33
    log[0xD1] = 0x33

    # RX gain gaps
    for off in [0xD4, 0xD6, 0xD8, 0xDA, 0xDC]:
        log[off] = 0x00

    # SDIO MAC address (0x16A inside rtw8822cs_efuse.res0[0x4a])
    mac = bytearray([0x02, 0x1a, 0x2b,
                     random.randint(0x00, 0xfe),
                     random.randint(0x00, 0xff),
                     random.randint(0x00, 0xff)])
    log[0x16A:0x16A + 6] = mac

    # === Build physical map ===
    phy = bytearray(b'\xff' * PHY_SIZE)
    phy_idx = 0

    # Blocks that have non-0xff data (scanned automatically below)
    blocks_with_data = set()
    for blk in range(LOG_SIZE // 8):
        start = blk * 8
        block = bytes(log[start:start + 8])
        has_data = any(b != 0xff for b in block)
        if has_data:
            blocks_with_data.add(blk)

    blocks = sorted(blocks_with_data)
    print(f"Blocks with data: {blocks}")

    for blk in blocks:
        part = encode_block(log, blk)
        if not part:
            continue
        remaining = PHY_SIZE - phy_idx
        if len(part) > remaining:
            print(f"WARNING: block {blk} ({len(part)} bytes) "
                  f"exceeds space ({remaining}), truncating")
            break
        phy[phy_idx:phy_idx + len(part)] = part
        phy_idx += len(part)
        start = blk * 8
        print(f"  blk {blk:3d} [{start:04x}-{start+7:04x}]: "
              f"physical {len(part)} bytes")

    # Write the firmware file
    outpath = "rtl8822cs_efuse.bin"
    with open(outpath, "wb") as f:
        f.write(bytes(phy))
    print(f"\nWrote {outpath}")
    print(f"  Physical size: {PHY_SIZE} bytes")
    print(f"  Used:          {phy_idx} bytes")
    print(f"  Free:          {PHY_SIZE - phy_idx} bytes")

    # Verify: parse the physical map back and compare logical
    verify(log, phy, PHY_SIZE)

    # Hex dump
    with open("rtl8822cs_efuse.dump", "w") as f:
        for i in range(0, PHY_SIZE, 16):
            hexb = " ".join(f"{b:02x}" for b in phy[i:i+16])
            marker = "  <-- used" if i + 16 <= phy_idx else ""
            f.write(f"{i:04x}: {hexb}{marker}\n")
    print("Wrote rtl8822cs_efuse.dump")


def verify(expected_log, phy_map, phy_size):
    """Parse the physical map like the kernel does and verify correctness."""
    log = bytearray(b'\xff' * LOG_SIZE)
    protect_size = 0x04  # from rtw8822c_hw_spec

    phy_idx = 0
    while phy_idx < phy_size - protect_size:
        hdr1 = phy_map[phy_idx]
        hdr2 = phy_map[phy_idx + 1]

        if hdr1 == 0xff or ((hdr1 & 0x1f) == 0xf and hdr2 == 0xff):
            break

        if (hdr1 & 0x1f) == 0xf:
            blk_idx = ((hdr2 & 0xf0) >> 1) | ((hdr1 >> 5) & 0x07)
            word_en = hdr2 & 0xf
            phy_idx += 2
        else:
            blk_idx = (hdr1 & 0xf0) >> 4
            word_en = hdr1 & 0xf
            phy_idx += 1

        for i in range(4):
            if (word_en & (1 << i)):
                continue
            if phy_idx + 1 > phy_size - protect_size:
                print(f"VERIFY ERROR: block {blk_idx} overflows physical map")
                return
            log_idx = (blk_idx << 3) + (i << 1)
            if log_idx + 1 > LOG_SIZE:
                print(f"VERIFY ERROR: block {blk_idx} overflow logical map")
                return
            log[log_idx] = phy_map[phy_idx]
            log[log_idx + 1] = phy_map[phy_idx + 1]
            phy_idx += 2

    mismatches = 0
    for i in range(LOG_SIZE):
        if log[i] != expected_log[i]:
            mismatches += 1
            if mismatches <= 10:
                print(f"  MISMATCH at 0x{i:04x}: "
                      f"expected 0x{expected_log[i]:02x}, got 0x{log[i]:02x}")

    if mismatches == 0:
        print("  VERIFY: ALL OK — logical map matches expected values")
    else:
        print(f"  VERIFY: {mismatches} mismatches total")


if __name__ == "__main__":
    main()
