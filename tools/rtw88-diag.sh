#!/usr/bin/env bash
set -euo pipefail

# rtw88-diag.sh — Diagnostic wrapper for rtw88 debugfs interface
# Usage: rtw88-diag.sh <command> [args...]
#
# No rtwpriv exists for in-kernel rtw88; this script wraps the debugfs files.
# Path: /sys/kernel/debug/ieee80211/<phy>/rtw88/

DEFAULT_PHY="phy0"
RTW_DIR=""

usage() {
	cat <<EOF
Usage: $(basename "$0") [--phy phyN] <command> [args...]

Commands:
  ls                          List all debugfs files
  efuse                       Dump efuse map (hexdump)
  phy                         Show PHY info (RSSI, rates, EVM, SNR, CCA)
  coex                        Show coexistence info
  txpower                     Show TX power table
  rf-dump                     Dump all RF registers
  read-mac    <addr> [len]    Read MAC/BE register (len=1|2|4, default 4)
  write-mac   <addr> <val> <len>  Write MAC/BE register (len=1|2|4)
  read-rf     <path> <addr> [mask]  Read RF register (mask default 0xffffffff)
  write-rf    <path> <addr> <mask> <val>  Write RF register
  dump-mac    <page>          Dump MAC page (0..7, 10..17 hex) — e.g. dump-mac 0
  dump-bb     <page>          Dump BB page (8..0x41 hex) — e.g. dump-bb 8
  cam         <entry>         Dump CAM entry (0..31)
  fix-rate    [rate]          Get/set fixed TX rate (255 = disable)
  force-lowest [0|1]          Get/set force lowest basic rate
  coex-enable [0|1]           Get/set coexistence enabled
  edcca       [0|1]           Get/set EDCCA enabled
  fw-crash                    Trigger firmware crash/restart (write 1)
  dm-cap      [bit] [0|1]     Get/set DM capability (1=TXGAPK)
  h2c         <p0,p1,...p7>   Send H2C command (8 hex bytes)
  rsvd-page   <offset> <num>  Dump reserved page(s)

Options:
  --phy phyN    Use phyN instead of $DEFAULT_PHY (e.g. --phy phy1)
  -h, --help    Show this help
EOF
	exit ${1:-0}
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

find_rtw_dir() {
	local phy="$1"
	local d
	d="/sys/kernel/debug/ieee80211/$phy/rtw88"
	if [[ -d "$d" ]]; then
		RTW_DIR="$d"
		return 0
	fi
	d="/sys/kernel/debug/ieee80211/$phy/driver/rtw88"
	if [[ -d "$d" ]]; then
		RTW_DIR="$d"
		return 0
	fi
	die "debugfs directory not found for $phy (is rtw88 loaded and CONFIG_RTW88_DEBUGFS=y?)"
}

# --- file helpers ---

read_file() {
	local name="$1"
	cat "$RTW_DIR/$name"
}

write_file() {
	local name="$1" val="$2"
	echo -n "$val" > "$RTW_DIR/$name"
}

write_read() {
	# write a value then cat back
	local name="$1" val="$2"
	write_file "$name" "$val"
	read_file "$name"
}

# --- commands ---

cmd_ls() {
	ls -la "$RTW_DIR"
}

cmd_efuse() {
	if [[ -r "$RTW_DIR/efuse_map" ]]; then
		od -Ax -tx1 -v "$RTW_DIR/efuse_map"
	else
		cat "$RTW_DIR/efuse_map"
	fi
}

cmd_phy() {
	read_file phy_info
}

cmd_coex() {
	read_file coex_info
}

cmd_txpower() {
	read_file tx_pwr_tbl
}

cmd_rf_dump() {
	read_file rf_dump
}

cmd_read_mac() {
	local addr="${1:?missing addr}" len="${2:-4}"
	write_read read_reg "$addr $len"
}

cmd_write_mac() {
	local addr="${1:?missing addr}" val="${2:?missing val}" len="${3:?missing len (1|2|4)}"
	write_file write_reg "$addr $val $len"
	echo "wrote $addr <- 0x$val ($len bytes)"
}

cmd_read_rf() {
	local path="${1:?missing path}" addr="${2:?missing addr}" mask="${3:-0xffffffff}"
	write_read rf_read "$path $addr $mask"
}

cmd_write_rf() {
	local path="${1:?missing path}" addr="${2:?missing addr}" mask="${3:?missing mask}" val="${4:?missing val}"
	write_file rf_write "$path $addr $mask $val"
	echo "wrote RF path=$path addr=0x$addr mask=0x$mask <- 0x$val"
}

cmd_dump_mac() {
	local page="${1:?missing page (e.g. 0, 5, 10)}"
	local f
	printf -v f "mac_%s" "$page"
	if [[ -f "$RTW_DIR/$f" ]]; then
		read_file "$f"
	else
		die "no such MAC page: $f (try: 0-7, 10-17)"
	fi
}

cmd_dump_bb() {
	local page="${1:?missing page (e.g. 8, 1a, 2c)}"
	local f
	printf -v f "bb_%s" "$page"
	if [[ -f "$RTW_DIR/$f" ]]; then
		read_file "$f"
	else
		die "no such BB page: $f (try: 8-1f, 2c-2d, 40-41)"
	fi
}

cmd_cam() {
	local entry="${1:-0}"
	write_read dump_cam "$entry"
}

cmd_fix_rate() {
	if [[ $# -ge 1 ]]; then
		write_file fix_rate "${1}"
	fi
	read_file fix_rate
}

cmd_force_lowest() {
	if [[ $# -ge 1 ]]; then
		write_file force_lowest_basic_rate "${1}"
	fi
	read_file force_lowest_basic_rate
}

cmd_coex_enable() {
	if [[ $# -ge 1 ]]; then
		write_file coex_enable "${1}"
	fi
	read_file coex_enable
}

cmd_edcca() {
	if [[ $# -ge 1 ]]; then
		write_file edcca_enable "${1}"
	fi
	read_file edcca_enable
}

cmd_fw_crash() {
	write_file fw_crash 1
	echo "firmware crash triggered (driver will restart)"
}

cmd_dm_cap() {
	if [[ $# -ge 2 ]]; then
		# bit number, positive = disable, negative = enable
		local bit="${1}" val
		# dm_cap: write positive to disable, negative to enable
		[[ "${2}" == "1" ]] && val="${bit}" || val="-${bit}"
		write_file dm_cap "$val"
	fi
	read_file dm_cap
}

cmd_h2c() {
	local params="${1:?missing 8 hex bytes, comma-separated}"
	write_file h2c "$params"
	echo "H2C sent: $params"
}

cmd_rsvd_page() {
	local offset="${1:?missing offset}" num="${2:?missing page count}"
	write_read rsvd_page "$offset $num"
}

# --- main ---

PHY="$DEFAULT_PHY"
COMMAND=""

ARGS=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--phy) PHY="$2"; shift 2 ;;
		-h|--help) usage 0 ;;
		-*) die "unknown option: $1" ;;
		*) COMMAND="$1"; shift; ARGS=("$@"); break ;;
	esac
done

[[ -z "$COMMAND" ]] && usage 1

find_rtw_dir "$PHY"

case "$COMMAND" in
	ls)           cmd_ls "$@" ;;
	efuse)        cmd_efuse "$@" ;;
	phy)          cmd_phy "$@" ;;
	coex)         cmd_coex "$@" ;;
	txpower)      cmd_txpower "$@" ;;
	rf-dump)      cmd_rf_dump "$@" ;;
	read-mac)     cmd_read_mac "${ARGS[@]}" ;;
	write-mac)    cmd_write_mac "${ARGS[@]}" ;;
	read-rf)      cmd_read_rf "${ARGS[@]}" ;;
	write-rf)     cmd_write_rf "${ARGS[@]}" ;;
	dump-mac)     cmd_dump_mac "${ARGS[@]}" ;;
	dump-bb)      cmd_dump_bb "${ARGS[@]}" ;;
	cam)          cmd_cam "${ARGS[@]}" ;;
	fix-rate)     cmd_fix_rate "${ARGS[@]}" ;;
	force-lowest) cmd_force_lowest "${ARGS[@]}" ;;
	coex-enable)  cmd_coex_enable "${ARGS[@]}" ;;
	edcca)        cmd_edcca "${ARGS[@]}" ;;
	fw-crash)     cmd_fw_crash "${ARGS[@]}" ;;
	dm-cap)       cmd_dm_cap "${ARGS[@]}" ;;
	h2c)          cmd_h2c "${ARGS[@]}" ;;
	rsvd-page)    cmd_rsvd_page "${ARGS[@]}" ;;
	*)            die "unknown command: $COMMAND (use -h for help)" ;;
esac
