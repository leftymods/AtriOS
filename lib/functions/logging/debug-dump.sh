#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

function debug_dict() {
	if [[ "${SHOW_DEBUG}" != "yes" ]]; then
		return 0
	fi
	local dict_name="$1"
	declare -n dict="${dict_name}"
	for key in "${!dict[@]}"; do
		debug_var "${dict_name}[${key}]"
	done
}

function debug_var() {
	if [[ "${SHOW_DEBUG}" != "yes" ]]; then
		return 0
	fi
	local varname="$1"
	local -a var_val_array=("${!varname}")
	display_alert "${gray_color:-}# ${yellow_color:-}${varname}${normal_color:-}=${bright_yellow_color:-}${var_val_array[*]@Q}${ansi_reset_color:-}" "" "info"
}
