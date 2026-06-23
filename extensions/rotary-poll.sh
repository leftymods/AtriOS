#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025-2026 leftymods
#

# Userspace rotary encoder polling daemon for GPIOs without IRQ support.
# Polls two GPIO lines every 10ms, decodes Gray code, emits REL_DIAL via uinput.
# To use: set ROTARY_GPIO_A and ROTARY_GPIO_B in board config and enable_extension rotary-poll.

function extension_prepare_config__rotary_poll() {
	if [[ -z "${ROTARY_GPIO_A}" || -z "${ROTARY_GPIO_B}" ]]; then
		exit_with_error "Extension: ${EXTENSION}: ${BOARD} - ROTARY_GPIO_A and ROTARY_GPIO_B must be set"
	fi
}

function post_family_tweaks_bsp__rotary_poll_add_binary() {
	display_alert "Extension: ${EXTENSION}: ${BOARD}" "installing rotary-poll daemon" "info"
	: "${destination:?destination is not set}"

	declare bin_dir="/usr/local/sbin"
	declare bin_path="${bin_dir}/rotary-poll"
	declare src_path="${SRC}/extensions/rotary-poll"

	run_host_command_logged mkdir -pv "${destination}${bin_dir}"

	# Binary is pre-compiled; copy it
	run_host_command_logged cp -v "${src_path}" "${destination}${bin_path}"
	run_host_command_logged chmod -v +x "${destination}${bin_path}"

	# Create systemd service
	cat <<- ROTARY_SERVICE > "$destination"/lib/systemd/system/rotary-poll.service
		[Unit]
		Description=${BOARD} Rotary Encoder Polling Daemon
		After=local-fs.target
		StartLimitIntervalSec=0

		[Service]
		Type=simple
		ExecStart=${bin_path} ${ROTARY_GPIO_A} ${ROTARY_GPIO_B}
		Restart=always
		RestartSec=2

		[Install]
		WantedBy=multi-user.target
	ROTARY_SERVICE

	return 0
}

function post_family_tweaks__rotary_poll_enable_service() {
	display_alert "Extension: ${EXTENSION}: ${BOARD}" "enabling rotary-poll service" "info"
	chroot_sdcard systemctl --no-reload enable "rotary-poll.service"
	return 0
}
