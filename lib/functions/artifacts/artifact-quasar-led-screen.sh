#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods

function artifact_quasar-led-screen_config_dump() {
	artifact_input_variables[ARCH]="${ARCH}"
	artifact_input_variables[BOARD]="${BOARD}"
}

function artifact_quasar-led-screen_prepare_version() {
	artifact_version="undetermined"
	artifact_version_reason="undetermined"

	declare short_hash_size=4
	declare fake_unchanging_base_version="1"

	declare hash_files="undetermined"
	calculate_hash_for_bash_deb_artifact "compilation/packages/quasar-led-screen-deb.sh"
	declare bash_hash="${hash_files}"
	declare bash_hash_short="${bash_hash:0:${short_hash_size}}"

	declare hash_files="undetermined"
	calculate_hash_for_all_files_in_dirs "${SRC}/packages/quasar-led-screen"
	declare src_hash="${hash_files}"
	declare src_hash_short="${src_hash:0:${short_hash_size}}"

	artifact_version="${fake_unchanging_base_version}-B${bash_hash_short}-S${src_hash_short}"

	declare -a reasons=(
		"AtriOS quasar-led-screen"
		"framework bash hash \"${bash_hash}\""
		"source files hash \"${src_hash}\""
	)

	artifact_version_reason="${reasons[*]}"

	artifact_map_packages=(["quasar-led-screen"]="quasar-led-screen")

	artifact_name="quasar-led-screen"
	artifact_type="deb"
	artifact_deb_repo="global"
	artifact_deb_arch="${ARCH}"

	artifact_debs_reversion_functions+=("reversion_quasar-led-screen_deb_contents")

	return 0
}

function artifact_quasar-led-screen_build_from_sources() {
	LOG_SECTION="compile_quasar-led-screen" do_with_logging compile_quasar-led-screen
}

function artifact_quasar-led-screen_cli_adapter_pre_run() {
	declare -g ATRIOS_COMMAND_REQUIRE_BASIC_DEPS="yes"
	cli_standard_relaunch_docker_or_sudo
}

function artifact_quasar-led-screen_cli_adapter_config_prep() {
	use_board="yes" allow_no_family="no" skip_kernel="no" prep_conf_main_minimal_ni < /dev/null
}

function artifact_quasar-led-screen_get_default_oci_target() {
	artifact_oci_target_base="${GHCR_SOURCE}/leftymods/coreos/"
}

function artifact_quasar-led-screen_is_available_in_local_cache() {
	is_artifact_available_in_local_cache
}

function artifact_quasar-led-screen_is_available_in_remote_cache() {
	is_artifact_available_in_remote_cache
}

function artifact_quasar-led-screen_obtain_from_remote_cache() {
	obtain_artifact_from_remote_cache
}

function artifact_quasar-led-screen_deploy_to_remote_cache() {
	upload_artifact_to_oci
}