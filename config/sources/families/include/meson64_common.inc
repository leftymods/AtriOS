#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2023 Igor Pecovnik, igor@AtriOS.com
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/
#
UBOOT_TARGET_MAP="${UBOOT_TARGET_MAP:-u-boot-dtb.img;;u-boot.bin.sd.bin:u-boot.bin u-boot-dtb.img}"
BOOTSCRIPT="boot-meson64.cmd:boot.cmd"
BOOTENV_FILE='meson.txt'
LINUXFAMILY=meson64
ARCH=arm64
SERIALCON=ttyAML0
SRC_LOADADDR='LOADADDR=0x1080000'
BOOTBRANCH="${BOOTBRANCH_BOARD:-"tag:v2022.07"}"
BOOTPATCHDIR="${BOOTPATCHDIR:-"v2022.07"}"
OVERLAY_PREFIX='meson'
OVERLAY_DIR="/boot/dtb/amlogic/overlay"

# This is an extension method, put directly in meson64_common. A "built-in" extension if you will.
# Bring in LibreELEC's amlogic-boot-fip repo, which is the authoritative source for FIP blobs.
# To add FIP blobs for a new board, send a PR there, they're awesome.
function fetch_sources_tools__libreelec_amlogic_fip() {
	fetch_from_repo "https://github.com/LibreELEC/amlogic-boot-fip" "amlogic-boot-fip" "branch:master"
}

# this family does not need it
ATF_COMPILE="no"

# Disable BTF to avoid running out of RAM during kernel build
KERNEL_BTF="no"

# Set CPUMIN et al if not already set in the board config.
CPUMIN=${CPUMIN:-500000}
CPUMAX=${CPUMAX:-1536000}
GOVERNOR=${GOVERNOR:-ondemand}

case $BRANCH in

	oldlts)
		declare -g KERNEL_MAJOR_MINOR="6.12"
		;;
	current)
		declare -g KERNEL_MAJOR_MINOR="6.18"
		declare -g KERNELSOURCE="https://github.com/leftymods/linux-6.18.y.git"
		declare -g KERNELBRANCH="commit:248900b6b97c0ed991b47fc486b05787bec95d1c"
		declare -g DISABLE_KERNEL_PATCHES="no"
		;;

esac

# Disable unused SoC families; saves compile time on Amlogic-only builds.
function custom_kernel_config__disable_unused_arches() {
	opts_n+=("ARCH_SUNXI")
	opts_n+=("ARCH_ROCKCHIP")
	# Unused Wi-Fi/BT vendors
	opts_n+=("WLAN_VENDOR_ADMTEK" "WLAN_VENDOR_ATH" "WLAN_VENDOR_ATMEL"
		 "WLAN_VENDOR_BROADCOM" "WLAN_VENDOR_INTEL" "WLAN_VENDOR_INTERSIL"
		 "WLAN_VENDOR_MARVELL" "WLAN_VENDOR_QUANTENNA"
		 "WLAN_VENDOR_RALINK" "WLAN_VENDOR_MEDIATEK"
		 "WLAN_VENDOR_CISCO")
	opts_n+=("BRCMFMAC" "BRCMDBG" "P54_COMMON" "MWIFIEX" "MT76_CORE"
		 "MAC80211_HWSIM")
	# Unused SATA/Storage
	opts_n+=("SATA_MV" "SATA_SIL24" "AHCI_MTK" "AHCI_TEGRA"
		 "AHCI_IMX" "AHCI_MVEBU" "MUSB"  "PATA_PXA")
	# Unused Sound controllers
	opts_n+=("SND_SOC_FSL" "SND_SOC_INTEL" "SND_SOC_AMD"
		 "SND_SOC_ROCKCHIP" "SND_SOC_TEGRA" "SND_SOC_SOF_TOPLEVEL"
		 "SND_BCM2835" "SND_PXA2XX" "SND_SOC_SOF"
		 "SND_SUN4I" "SND_SUN8I" "SND_PCM_OSS" "SND_SEQUENCER_OSS")
	# Unused pincontrol
	opts_n+=("PINCTRL_INTEL" "PINCTRL_BAYTRAIL" "PINCTRL_CHERRYVIEW"
		 "PINCTRL_BROXTON" "PINCTRL_SUNRISEPOINT" "PINCTRL_GEMINILAKE"
		 "PINCTRL_ICELAKE" "PINCTRL_TIGERLAKE" "PINCTRL_ALDERLAKE" "PINCTRL_SUNXI")
	opts_n+=("CONFIG_FB_ATRI_LED_PANEL")
}

# Enable RTL8822CS debugfs for diagnostics
function custom_kernel_config__enable_rtw88_debugfs() {
	opts_y+=("CONFIG_RTW88_8822CS")
	opts_y+=("CONFIG_RTW88_DEBUGFS")
}

# Enable SY6045S audio amplifiers for Station Max
function custom_kernel_config__enable_sy6045s() {
	opts_y+=("CONFIG_SND_SOC_SY6045S")
	opts_y+=("CONFIG_BT_HCIUART_3WIRE")
	opts_y+=("CONFIG_BT_HCIUART_RTL")
	opts_m+=("CONFIG_SND_SOC_ES7210")
	opts_m+=("CONFIG_SND_SOC_ES8156")
	opts_m+=("CONFIG_SND_SOC_TLV320ADC3XXX")
	opts_m+=("CONFIG_SND_ALOOP")
	opts_y+=("CONFIG_IIO")
	opts_m+=("CONFIG_KEYBOARD_ADC")
	opts_m+=("CONFIG_LEDS_IS31FL32XX")
	opts_m+=("CONFIG_LEDS_CLASS_MULTICOLOR")
	opts_m+=("CONFIG_INPUT_GPIO_ROTARY_ENCODER")
	opts_m+=("CONFIG_RTC_DRV_PCF8563")
	opts_m+=("CONFIG_LTR390")
	opts_m+=("CONFIG_LTRF216A")
	opts_m+=("CONFIG_SERIO_AMS_DELTA")
	opts_m+=("CONFIG_GPIO_SYSFS")
}

# this helper function includes postprocess for s400 and its variants.
# $1 PATH for uboot blob repo
# $2 dir name in uboot blob repo
uboot_axg_postprocess_ng() {
	run_host_command_logged mv -v u-boot.bin bl33.bin

	run_host_command_logged $1/blx_fix.sh $1/$2/bl30.bin \
		$1/$2/zero_tmp \
		$1/$2/bl30_zero.bin \
		$1/$2/bl301.bin \
		$1/$2/bl301_zero.bin \
		$1/$2/bl30_new.bin bl30

	run_host_command_logged python3 $1/acs_tool.py $1/$2/bl2.bin \
		$1/$2/bl2_acs.bin \
		$1/$2/acs.bin 0

	run_host_command_logged $1/blx_fix.sh $1/$2/bl2_acs.bin \
		$1/$2/zero_tmp \
		$1/$2/bl2_zero.bin \
		$1/$2/bl21.bin \
		$1/$2/bl21_zero.bin \
		$1/$2/bl2_new.bin bl2

	run_host_x86_binary_logged $1/$2/aml_encrypt_axg --bl3sig --input $1/$2/bl30_new.bin \
		--output bl30_new.bin.enc \
		--level v3 --type bl30

	run_host_x86_binary_logged $1/$2/aml_encrypt_axg --bl3sig --input $1/$2/bl31.img \
		--output bl31.img.enc \
		--level v3 --type bl31

	run_host_x86_binary_logged $1/$2/aml_encrypt_axg --bl3sig --input bl33.bin --compress lz4 \
		--output bl33.bin.enc \
		--level v3 --type bl33

	run_host_x86_binary_logged $1/$2/aml_encrypt_axg --bl2sig --input $1/$2/bl2_new.bin \
		--output bl2.n.bin.sig

	run_host_x86_binary_logged $1/$2/aml_encrypt_axg --bootmk \
		--output u-boot.bin \
		--bl2 bl2.n.bin.sig \
		--bl30 bl30_new.bin.enc \
		--bl31 bl31.img.enc \
		--bl33 bl33.bin.enc --level v3
}

# this helper function includes postprocess for u200 and its variants.
# $1 PATH for uboot blob repo
# $2 family g12a or g12b
uboot_g12_postprocess() {
	run_host_command_logged mv -v u-boot.bin bl33.bin

	run_host_command_logged $1/blx_fix.sh $1/bl30.bin \
		zero_tmp \
		bl30_zero.bin \
		$1/bl301.bin \
		bl301_zero.bin \
		bl30_new.bin bl30

	run_host_command_logged $1/blx_fix.sh $1/bl2.bin \
		zero_tmp \
		bl2_zero.bin \
		$1/acs.bin \
		bl21_zero.bin \
		bl2_new.bin bl2

	run_host_x86_binary_logged $1/aml_encrypt_$2 --bl30sig \
		--input bl30_new.bin \
		--output bl30_new.bin.g12.enc \
		--level v3
	run_host_x86_binary_logged $1/aml_encrypt_$2 --bl3sig \
		--input bl30_new.bin.g12.enc \
		--output bl30_new.bin.enc \
		--level v3 --type bl30
	run_host_x86_binary_logged $1/aml_encrypt_$2 --bl3sig \
		--input $1/bl31.img \
		--output bl31.img.enc \
		--level v3 --type bl31
	run_host_x86_binary_logged $1/aml_encrypt_$2 --bl3sig \
		--input bl33.bin \
		--compress lz4 \
		--output bl33.bin.enc \
		--level v3 --type bl33
	run_host_x86_binary_logged $1/aml_encrypt_$2 --bl2sig \
		--input bl2_new.bin \
		--output bl2.n.bin.sig
	if [ -e $1/lpddr3_1d.fw ]; then
		run_host_x86_binary_logged $1/aml_encrypt_$2 --bootmk --output u-boot.bin \
			--bl2 bl2.n.bin.sig \
			--bl30 bl30_new.bin.enc \
			--bl31 bl31.img.enc \
			--bl33 bl33.bin.enc \
			--ddrfw1 $1/ddr4_1d.fw \
			--ddrfw2 $1/ddr4_2d.fw \
			--ddrfw3 $1/ddr3_1d.fw \
			--ddrfw4 $1/piei.fw \
			--ddrfw5 $1/lpddr4_1d.fw \
			--ddrfw6 $1/lpddr4_2d.fw \
			--ddrfw7 $1/diag_lpddr4.fw \
			--ddrfw8 $1/aml_ddr.fw \
			--ddrfw9 $1/lpddr3_1d.fw \
			--level v3
	else
		run_host_x86_binary_logged $1/aml_encrypt_$2 --bootmk --output u-boot.bin \
			--bl2 bl2.n.bin.sig \
			--bl30 bl30_new.bin.enc \
			--bl31 bl31.img.enc \
			--bl33 bl33.bin.enc \
			--ddrfw1 $1/ddr4_1d.fw \
			--ddrfw2 $1/ddr4_2d.fw \
			--ddrfw3 $1/ddr3_1d.fw \
			--ddrfw4 $1/piei.fw \
			--ddrfw5 $1/lpddr4_1d.fw \
			--ddrfw6 $1/lpddr4_2d.fw \
			--ddrfw7 $1/diag_lpddr4.fw \
			--ddrfw8 $1/aml_ddr.fw \
			--level v3
	fi

}

# this helper function includes postprocess for meson gxl and gxm.
# $1 PATH for uboot blob repo
uboot_gxl_postprocess_ng() {
	run_host_command_logged mv -v u-boot.bin bl33.bin

	run_host_command_logged $1/blx_fix.sh $1/bl30.bin \
		$1/zero_tmp \
		$1/bl30_zero.bin \
		$1/bl301.bin \
		$1/bl301_zero.bin \
		$1/bl30_new.bin bl30

	run_host_command_logged python3 $1/acs_tool.py $1/bl2.bin $1/bl2_acs.bin $1/acs.bin 0

	run_host_command_logged $1/blx_fix.sh $1/bl2_acs.bin \
		$1/zero_tmp \
		$1/bl2_zero.bin \
		$1/bl21.bin \
		$1/bl21_zero.bin \
		$1/bl2_new.bin bl2

	run_host_command_logged $1/aml_encrypt_gxl --bl3enc --input $1/bl30_new.bin
	run_host_command_logged $1/aml_encrypt_gxl --bl3enc --input $1/bl31.img
	run_host_command_logged $1/aml_encrypt_gxl --bl3enc --input bl33.bin
	run_host_command_logged $1/aml_encrypt_gxl --bl2sig --input $1/bl2_new.bin \
		--output bl2.n.bin.sig

	run_host_command_logged $1/aml_encrypt_gxl --bootmk --output u-boot.bin \
		--bl2 bl2.n.bin.sig \
		--bl30 $1/bl30_new.bin.enc \
		--bl31 $1/bl31.img.enc \
		--bl33 bl33.bin.enc
}

write_uboot_platform() {
	dd if=$1/u-boot.bin of=$2 bs=1 count=442 conv=fsync 2>&1
	dd if=$1/u-boot.bin of=$2 bs=512 skip=1 seek=1 conv=fsync 2>&1
}

family_tweaks_bsp() {
	mkdir -p $destination/etc/udev/rules.d
	mkdir -p $destination/usr/local/bin

	mkdir -p "$destination"/etc/X11/xorg.conf.d
	case "${BOARD}" in
		"odroidc2" | "nanopik2-s905" | "lepotato" | "sweet-potato" | "lafrite" | "khadas-vim1" | "khadas-vim2" | \
		"aml-c400-plus" | "aml-s9xx-box" | "bestv-r3300-l")
			cat <<- EOF > "$destination"/etc/X11/xorg.conf.d/02-driver.conf
				Section "OutputClass"
				    Identifier "Amlogic"
				    MatchDriver "meson"
				    Driver "modesetting"
				    Option "PrimaryGPU" "true"
				EndSection

				Section "Screen"
				    Identifier      "Default Screen"
				    Device          "Meson"
				    Monitor         "foo"
				    DefaultDepth  24
				    SubSection "Display"
				        Depth    24
				        Modes    "1920x1080" "1440x900" "1280x720" "1280x1024" "1280x960" "1024x768" "800x600" "640x480" "720x400"
				    EndSubSection
				EndSection
			EOF
			;;
		"jethubj200" | "odroidn2" | "atristation" | "khadas-vim2" | "odroidhc4" | "khadas-vim3" | "khadas-vim3l" | \
		"radxa-zero" | "radxa-zero2" | "bananapi-m2s" | "cainiao-cniot-core")
			cat <<- EOF > "$destination"/etc/X11/xorg.conf
				Section "Device"
				    Identifier  "DRM Graphics Acclerated"

				    ## Use modesetting and glamor
				    Driver      "modesetting"
				    Option      "AccelMethod"    "glamor"     ### "glamor" to enable 3D acceleration, "none" to disable.
				    Option      "DRI"            "2"
				    Option      "Dri2Vsync"      "true"
				    Option      "TripleBuffer"   "True"
				    ## End glamor configuration

				EndSection

				Section "OutputClass"
				    Identifier "Amlogic"
				    MatchDriver "meson"
				    Driver "modesetting"
				    Option "PrimaryGPU" "true"
				EndSection

				Section "Screen"
				    Identifier "Default Screen"
				    SubSection "Display"
				        Depth 24
				    EndSubSection
				EndSection
			EOF
			;;
	esac
}

# Install Rust yandex_mono_matrix_led driver + embed FPGA bitstream.
# Applied in kernel_make_config so it survives git reset.
function kernel_make_config__install_yandex_mono_matrix_led() {
	if linux-version compare "${KERNEL_MAJOR_MINOR}" lt "6.18"; then
		return 0
	fi

	local drvdir="${kernel_work_dir}/drivers/leds/yandex_mono_matrix_led"
	local src_rs="${SRC}/config/rust-driver/yandex_mono_matrix_led.rs"
	local src_bin="${SRC}/config/rust-driver/yandex_led_screen_fpga.bin"

	if [[ ! -f "${src_rs}" ]]; then
		exit_with_error "Rust driver not found: ${src_rs}"
	fi

	display_alert "Installing Yandex Mono Matrix LED driver" "yandex_mono_matrix_led" "info"
	run_host_command_logged mkdir -pv "${drvdir}"
	run_host_command_logged cp -v "${src_rs}" "${drvdir}/"
	run_host_command_logged cp -v "${src_bin}" "${drvdir}/"

	local leds_make="${kernel_work_dir}/drivers/leds/Makefile"
	local leds_kconfig="${kernel_work_dir}/drivers/leds/Kconfig"

	if ! grep -q 'CONFIG_LEDS_YANDEX_MONO_MATRIX' "${leds_kconfig}"; then
		display_alert "Adding Kconfig entry for yandex_mono_matrix_led" "" "info"
		cat >> "${leds_kconfig}" <<- 'KCONFIG_EOF'

		config LEDS_YANDEX_MONO_MATRIX
			tristate "Yandex Station Max Mono Matrix LED Driver (Rust)"
			depends on RUST && OF
			help
			  Rust driver for the 25x16 monochrome LED matrix panel on
			  Yandex Station Max (Gowin FPGA, SPI + JTAG).
			  Embeds the FPGA bitstream and flashes at probe.
		KCONFIG_EOF
	fi

	if ! grep -q 'yandex_mono_matrix_led' "${leds_make}"; then
		display_alert "Adding Makefile entry for yandex_mono_matrix_led" "" "info"
		echo 'obj-$(CONFIG_LEDS_YANDEX_MONO_MATRIX) += yandex_mono_matrix_led/' >> "${leds_make}"
	fi
}

# Enable the Rust LED driver in kernel config (only if rustc is available)
function custom_kernel_config__enable_yandex_mono_matrix_led() {
	if ! command -v rustc &>/dev/null; then
		display_alert "rustc not found, skipping Rust LED driver" "CONFIG_LEDS_YANDEX_MONO_MATRIX" "info"
		return 0
	fi
	opts_y+=("CONFIG_LEDS_YANDEX_MONO_MATRIX")
	opts_y+=("CONFIG_RUST")
}
