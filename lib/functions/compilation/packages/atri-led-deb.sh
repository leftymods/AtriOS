#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

compile_atri-led() {
	: "${artifact_version:?artifact_version is not set}"

	declare cleanup_id="" tmp_dir=""
	prepare_temp_dir_in_workdir_and_schedule_cleanup "deb-atri-led" cleanup_id tmp_dir # namerefs

	declare atri_led_dir="atri-led"
	mkdir -p "${tmp_dir}/${atri_led_dir}"

	declare destination="${tmp_dir}/${atri_led_dir}"

	run_host_command_logged mkdir -p "${destination}"/{DEBIAN,usr/bin,usr/lib,usr/include}
	run_host_command_logged mkdir -p "${destination}"/{etc/atriled/animations,etc/udev/rules.d}
	run_host_command_logged mkdir -p "${destination}"/{lib/systemd/system,lib/firmware}

	cd "${destination}" || exit_with_error "can't change directory"

	# set up control file (Depends is added by reversion function)
	cat <<- END > DEBIAN/control
		Package: atri-led
		Version: ${artifact_version}
		Architecture: ${ARCH}
		Maintainer: $MAINTAINER <$MAINTAINERMAIL>
		Section: universe/utils
		Priority: optional
		Description: AtriStation LED ring and SPI screen tools
		 Provides atrled, atrledctl for LED ring control via I2C,
		 and quasar_led_test/text/demo/info for 25x16 SPI LED screen.
		 Includes systemd service for boot animation and screen
		 test/demo utilities.
	END

	# Compile C source for target architecture
	display_alert "Compiling atri-led" "CC=${KERNEL_COMPILER}gcc" "info"
	declare -g orig_dir="${SRC}/packages/atri-led"

	run_host_command_logged make -C "${orig_dir}" clean

	run_host_command_logged \
		make -C "${orig_dir}" \
		CC="${KERNEL_COMPILER}gcc" \
		DESTDIR="${destination}" \
		install

	# Copy configuration files from source package
	run_host_command_logged cp -rv "${orig_dir}"/etc/atriled/animations/*.anim "${destination}"/etc/atriled/animations/
	run_host_command_logged cp -rv "${orig_dir}"/etc/udev/rules.d/99-atri-led.rules "${destination}"/etc/udev/rules.d/
	# Copy firmware files if they exist
	for fw in "${orig_dir}"/lib/firmware/*.bin; do
		[[ -s "${fw}" ]] && run_host_command_logged cp -v "${fw}" "${destination}"/lib/firmware/
	done

	# Copy maintainer scripts
	run_host_command_logged cp "${orig_dir}"/debian/{postinst,prerm} "${destination}"/DEBIAN/
	chmod 755 "${destination}"/DEBIAN/{postinst,prerm}

	# fixing permissions
	find "${destination}" -print0 2> /dev/null | xargs -0r chown --no-dereference 0:0
	find "${destination}" ! -type l -print0 2> /dev/null | xargs -0r chmod 'go=rX,u+rw,a-s'

	dpkg_deb_build "${destination}" "atri-led"

	done_with_temp_dir "${cleanup_id}"

	display_alert "Done building atri-led package" "${destination}" "debug"
}

function reversion_atri-led_deb_contents() {
	if [[ "${1}" != "atri-led" ]]; then
		return 0
	fi
	display_alert "Reversion" "reversion_atri-led_deb_contents: '$*'" "debug"

	cat <<- EOF >> "${control_file_new}"
		Depends: libc6
	EOF

	return 0
}
