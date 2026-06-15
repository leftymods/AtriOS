#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

function github_actions_add_output() {
	# if CI is not GitHub Actions, do nothing
	if [[ "${GITHUB_ACTIONS}" != "true" ]]; then
		display_alert "Not running in GitHub Actions, not adding output" "'${*}'" "debug"
		return 0
	fi

	if [[ ! -f "${GITHUB_OUTPUT}" ]]; then
		exit_with_error "GITHUB_OUTPUT file not found '${GITHUB_OUTPUT}'"
	fi

	local output_name="$1"
	shift
	local output_value="$*"

	echo "${output_name}=${output_value}" >> "${GITHUB_OUTPUT}"
	display_alert "Added GHA output" "'${output_name}'='${output_value}'" "ext"
}
