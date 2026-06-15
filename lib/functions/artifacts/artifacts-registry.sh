#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

function AtriOS_register_artifacts() {

	declare -g -A AtriOS_ARTIFACTS_TO_HANDLERS_DICT=(
		# deb-tar
		["kernel"]="kernel"

		# deb
		["u-boot"]="uboot"
		["uboot"]="uboot"
		["firmware"]="firmware"
		["full_firmware"]="full_firmware"
		["fake_ubuntu_advantage_tools"]="fake_ubuntu_advantage_tools"
		["atrios-zsh"]="atrios-zsh"
		["atrios-zsh"]="atrios-zsh"
		["atrios-plymouth-theme"]="atrios-plymouth-theme"
		["atrios-plymouth-theme"]="atrios-plymouth-theme"
		["atrios-base-files"]="atrios-base-files"
		["atrios-base-files"]="atrios-base-files"
		["atrios-bsp-cli"]="atrios-bsp-cli"
		["atrios-bsp-cli"]="atrios-bsp-cli"

		# tar.zst
		["rootfs"]="rootfs"
	)

}
