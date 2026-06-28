#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

#!/usr/bin/env bash

function kernel_main_patching_python() {
	prepare_python_and_pip

	# outer scope variables: kernel_drivers_patch_file kernel_drivers_patch_hash

	declare patch_debug="${SHOW_DEBUG:-${DEBUG_PATCHING:-"no"}}"
	declare temp_file_for_output
	temp_file_for_output="$(mktemp)" # Get a temporary file for the output.

	# array with all parameters; will be auto-quoted by bash's @Q modifier below
	declare -a params_quoted=(
		"${PYTHON3_VARS[@]}"                                  # Default vars, from prepare_python_and_pip
		"LOG_DEBUG=${patch_debug}"                            # Logging level for python.
		"SRC=${SRC}"                                          # AtriOS root
		"OUTPUT=${temp_file_for_output}"                      # Output file for the python script.
		"ASSET_LOG_BASE=$(print_current_asset_log_base_file)" # base file name for the asset log; to write .md summaries.
		"PATCH_TYPE=kernel"                                   # or, u-boot, or, atf
		"PATCH_DIRS_TO_APPLY=${KERNELPATCHDIR}"               # A space-separated list of directories to apply...
		"USERPATCHES_PATH=${USERPATCHES_PATH}"                # Needed to find the userpatches.
		#"BOARD="                                             # BOARD is needed for the patchset selection logic; mostly for u-boot. empty for kernel.
		#"TARGET="                                            # TARGET is need for u-boot's SPI/SATA etc selection logic. empty for kernel
		# For table generation to fit into the screen, or being large when in GHA.
		"COLUMNS=${COLUMNS:-160}"
		"COLORFGBG=${COLORFGBG}"
		"GITHUB_ACTIONS=${GITHUB_ACTIONS}"
		# Needed so git can find the global .gitconfig, and Python can parse the PATH to determine which git to use.
		"PATH=${PATH}"
		"HOME=${HOME}"
		# What to do?
		"APPLY_PATCHES=yes"                                                    # Apply the patches to the filesystem. Does not imply git commiting. If no, still exports the hash.
		"PATCHES_TO_GIT=${PATCHES_TO_GIT:-no}"                                 # Commit to git after applying the patches.
		"REWRITE_PATCHES=${REWRITE_PATCHES:-no}"                               # Rewrite the original patch files after git commiting.
		"REWRITE_PATCHES_NEEDING_REBASE=${REWRITE_PATCHES_NEEDING_REBASE:-no}" # Only rewrite those patch files in need of a rebase.
		# Git dir, revision, and target branch
		"GIT_WORK_DIR=${kernel_work_dir}"                                # "Where to apply patches?"
		"BASE_GIT_REVISION=${kernel_git_revision}"                       # The revision we're building/patching. Python will reset and clean to this.
		"BRANCH_FOR_PATCHES=kernel-${LINUXFAMILY}-${KERNEL_MAJOR_MINOR}" # When applying patches-to-git, use this branch.
		# Lenience: allow problematic patches to be applied.
		"ALLOW_RECREATE_EXISTING_FILES=yes"       # Allow patches to recreate files that already exist.
		"GIT_ARCHEOLOGY=${GIT_ARCHEOLOGY:-no}"    # Allow git to do some archaeology to find the original patch's owners; used when patching to git/rewriting.
		"FAST_ARCHEOLOGY=${FAST_ARCHEOLOGY:-yes}" # Does archeology even further by looking for history from other patch files with the same name
		# Pass the maintainer info, used for commits.
		"MAINTAINER_NAME=${MAINTAINER}"      # Name of the maintainer
		"MAINTAINER_EMAIL=${MAINTAINERMAIL}" # Email of the maintainer
		# Pass in the drivers extra patches and hashes; will be applied _first_, before series.
		"EXTRA_PATCH_FILES_FIRST=${kernel_drivers_patch_file}"  # Is a space-separated list.
		"EXTRA_PATCH_HASHES_FIRST=${kernel_drivers_patch_hash}" # Is a space-separated list.
	)
	display_alert "Calling Python patching script for kernel:" "${KERNELSOURCE} - ${KERNELBRANCH}" "info"
	display_alert "Using kernel patch dir:" "${KERNELPATCHDIR}" "info"

	# "raw_command" is only for logging purposes.
	raw_command="[...shortened kernel patching...] ${PYTHON3_INFO[BIN]} ${SRC}/lib/tools/patching.py" \
		run_host_command_logged env -i "${params_quoted[@]@Q}" "${PYTHON3_INFO[BIN]}" "${SRC}/lib/tools/patching.py"
	run_host_command_logged cat "${temp_file_for_output}"
	# shellcheck disable=SC1090
	source "${temp_file_for_output}" # SOURCE IT!
	run_host_command_logged rm -f "${temp_file_for_output}"
	return 0
}

function kernel_main_patching() {
	# kernel_drivers_create_patches will fill the variables below
	declare kernel_drivers_patch_file kernel_drivers_patch_hash
	LOG_SECTION="kernel_drivers_create_patches" do_with_logging do_with_hooks kernel_drivers_create_patches "${kernel_work_dir}" "${kernel_git_revision}"

	# Python patching will git reset to the kernel SHA1 git revision, and remove all untracked files.
	LOG_SECTION="kernel_main_patching_python" do_with_logging do_with_hooks kernel_main_patching_python

	# Remove broken/unwanted board DTS files added by Armbian auto-patcher
	# meson-g12b-a311d-cainiao-cniot-core.dts: references non-existent pwm_d_a_pins
	declare cainiao_dts="${kernel_work_dir}/arch/arm64/boot/dts/amlogic/meson-g12b-a311d-cainiao-cniot-core.dts"
	if [[ -f "$cainiao_dts" ]]; then
		rm -f "$cainiao_dts"
		sed -i '/meson-g12b-a311d-cainiao-cniot-core.dtb/d' "${kernel_work_dir}/arch/arm64/boot/dts/amlogic/Makefile"
		display_alert "Removed broken board DTS" "meson-g12b-a311d-cainiao-cniot-core (pwm_d_a_pins missing)" "warn"
	fi

	# STOP HERE, for cli support for patching tools.
	if [[ "${PATCH_ONLY}" == "yes" ]]; then
		return 0
	fi

	# Interactive!!!
	[[ $CREATE_PATCHES == yes ]] && userpatch_create "kernel" # create patch for manual source changes

	return 0 # there is a shortcircuit above
}
