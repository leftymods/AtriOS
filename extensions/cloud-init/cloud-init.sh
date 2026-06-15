#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025-2026 leftymods
# This file is a part of the AtriOS Build Framework https://github.com/leftymods/CoreOS/
#

# Minimalistic implementation of Cloud-Init for AtriOS
# Functionaly equivalent to the implementation by Ubuntu for the Raspberri PI distribution

# Implementaiton is based on the NoCLoud data source that will use
# the FAT partition with armbi_boot label to source the configuration from

# The Cloud init files in the boot partition are meant to be replaced with user provided ones, they are empty
# of configurations except for setting hostname and DHCP on ethernet adapters.

# This extension also disables AtriOS-first-run

# Cloud-Init image marker
function extension_prepare_config__ci_image_suffix() {
	# Add to image suffix.
	EXTRA_IMAGE_SUFFIXES+=("-ci")
}

function extension_prepare_config__prepare_ci() {
	# Cloud Init related packages selected from Ubuntu RPI distirbution
	display_alert "Extension: ${EXTENSION}: Adding extra packages to image" "cloud-init cloud-initramfs-dyn-netconf" "info"
	add_packages_to_image cloud-init cloud-initramfs-dyn-netconf
}

function extension_prepare_config__ci_compatibility_check() {
	# We require fat boot partition, will change and if the user provided another type, will fail.
	if [[ -z "${BOOTFS_TYPE}" ]]; then
		declare -g BOOTFS_TYPE="fat"
		display_alert "Extension: ${EXTENSION}: Changing BOOTFS_TYPE" "cloud_init requires a fat partition" "warn"
	fi

	if [[ "${BOOTFS_TYPE}" != "fat" ]]; then
		exit_with_error "Extension: ${EXTENSION}: BOOTFS_TYPE ${BOOTFS_TYPE} not compatible with cloud-init"
	fi
}

function pre_customize_image__inject_cloud_init_config() {
	# Copy the NoCLoud Cloud-Init Configuration
	display_alert "Extension: ${EXTENSION}: Configuring" "cloud-init" "info"
	local config_src="${EXTENSION_DIR}/config"
	local config_dst="${SDCARD}/etc/cloud/cloud.cfg.d"
	run_host_command_logged cp "${config_src}"/* "${config_dst}"

	# Provide default cloud-init files
	display_alert "Extension: ${EXTENSION}: Defaults" "cloud-init" "info"
	local defaults_src="${EXTENSION_DIR}/defaults"
	local defaults_dst="${SDCARD}/boot"
	run_host_command_logged cp "${defaults_src}"/* "${defaults_dst}"
	return 0
}

# @TODO: would be better to have "AtriOS first run" as an extension that can be disabled
function pre_customize_image__disable_AtriOS_first_run() {
	display_alert "Extension: ${EXTENSION}: Disabling" "AtriOS firstrun" "info"

	# Clean up default profile and network
	rm -f "${SDCARD}"/etc/profile.d/AtriOS-check-first-*
	rm -f "${SDCARD}"/etc/netplan/AtriOS-*

	# remove any networkd config leftover from AtriOS build
	rm -f "${SDCARD}"/etc/systemd/network/*.network || true

	# cleanup -- cloud-init makes some AtriOS stuff actually get in the way
	[[ -f "${SDCARD}/boot/AtriOS_first_run.txt.template" ]] && rm -f "${SDCARD}/boot/AtriOS_first_run.txt.template"
	[[ -f "${SDCARD}/root/.not_logged_in_yet" ]] && rm -f "${SDCARD}/root/.not_logged_in_yet"

}
