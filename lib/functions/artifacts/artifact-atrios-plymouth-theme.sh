#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

function artifact_atrios-plymouth-theme_config_dump() {
	# artifact_input_variables: None, for atrios-plymouth-theme.
	:
}

function artifact_atrios-plymouth-theme_prepare_version() {
	artifact_version="undetermined"        # outer scope
	artifact_version_reason="undetermined" # outer scope

	declare short_hash_size=4

	declare fake_unchanging_base_version="1"

	# get the hashes of the lib/ bash sources involved...
	declare hash_files="undetermined"
	calculate_hash_for_bash_deb_artifact "compilation/packages/atrios-plymouth-theme-deb.sh" "${SRC}/packages/plymouth-theme-AtriOS/AtriOS.plymouth"
	declare bash_hash="${hash_files}"
	declare bash_hash_short="${bash_hash:0:${short_hash_size}}"

	# outer scope
	artifact_version="${fake_unchanging_base_version}-B${bash_hash_short}"

	declare -a reasons=(
		"AtriOS atrios-plymouth-theme"
		"framework bash hash \"${bash_hash}\""
	)

	artifact_version_reason="${reasons[*]}" # outer scope

	artifact_map_packages=(["atrios-plymouth-theme"]="atrios-plymouth-theme")

	artifact_name="atrios-plymouth-theme"
	artifact_type="deb"
	artifact_deb_repo="global"
	artifact_deb_arch="all"

	return 0
}

function artifact_atrios-plymouth-theme_build_from_sources() {
	LOG_SECTION="compile_atrios-plymouth-theme" do_with_logging compile_atrios-plymouth-theme
}

function artifact_atrios-plymouth-theme_cli_adapter_pre_run() {
	declare -g ATRIOS_COMMAND_REQUIRE_BASIC_DEPS="yes" # Require prepare_host_basic to run before the command.

	# "gimme root on a Linux machine"
	cli_standard_relaunch_docker_or_sudo
}

function artifact_atrios-plymouth-theme_cli_adapter_config_prep() {
	use_board="no" prep_conf_main_minimal_ni < /dev/null # no stdin for this, so it bombs if tries to be interactive.
}

function artifact_atrios-plymouth-theme_get_default_oci_target() {
	artifact_oci_target_base="${GHCR_SOURCE}/leftymods/coreos/"
}

function artifact_atrios-plymouth-theme_is_available_in_local_cache() {
	is_artifact_available_in_local_cache
}

function artifact_atrios-plymouth-theme_is_available_in_remote_cache() {
	is_artifact_available_in_remote_cache
}

function artifact_atrios-plymouth-theme_obtain_from_remote_cache() {
	obtain_artifact_from_remote_cache
}

function artifact_atrios-plymouth-theme_deploy_to_remote_cache() {
	upload_artifact_to_oci
}
