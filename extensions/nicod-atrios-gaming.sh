#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025-2026 leftymods
# This file is a part of the AtriOS Build Framework https://github.com/leftymods/CoreOS/
#

# Creates a launcher script for NicoD's AtriOS-gaming project.
# Script will clone (or pull if already cloned) from NicoD's repo and run his script.

function extension_prepare_config__800_nicod_launcher() {
	EXTRA_IMAGE_SUFFIXES+=("-gaming") # global array; '800' hook is pretty much at the end
	return 0
}

function pre_customize_image__add_nicod_launcher() {
	display_alert "Adding NicoD's AtriOS-gaming launcher" "${EXTENSION}" "info"

	local launcher_dir="${SDCARD}/usr/local/bin"
	local launcher_file="${launcher_dir}/nicod-atrios-gaming"
	run_host_command_logged mkdir -pv "${launcher_dir}"

	cat <<- 'NICOD_GAMING_LAUNCHER_SCRIPT' > "${launcher_file}"
		#!/usr/bin/env bash
		if [[ ! -d ~/AtriOS-gaming ]]; then
			git clone https://github.com/NicoD-SBC/AtriOS-gaming.git ~/AtriOS-gaming
		fi
		cd ~/AtriOS-gaming
		git pull || true
		bash AtriOS-gaming.sh "$@"
	NICOD_GAMING_LAUNCHER_SCRIPT

	run_host_command_logged chmod -v +x "${launcher_file}"
	display_alert "Added NicoD's AtriOS-gaming launcher" "${EXTENSION}" "info"
}
