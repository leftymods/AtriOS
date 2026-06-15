#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

function cli_undecided_pre_run() {
	# If undecided, run the 'build' command.
	display_alert "cli_undecided_pre_run" "func cli_undecided_pre_run go to build" "debug"
	ATRIOS_CHANGE_COMMAND_TO="build"
}

function cli_undecided_run() {
	exit_with_error "Should never run the undecided command. How did this happen?"
}
