#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods

compile_quasar-led-screen() {
	: "${artifact_version:?artifact_version is not set}"

	declare cleanup_id="" tmp_dir=""
	prepare_temp_dir_in_workdir_and_schedule_cleanup "deb-quasar-led-screen" cleanup_id tmp_dir

	declare quasar_led_dir="quasar-led-screen"
	mkdir -p "${tmp_dir}/${quasar_led_dir}"

	declare destination="${tmp_dir}/${quasar_led_dir}"

	run_host_command_logged mkdir -p "${destination}"/{DEBIAN,usr/bin}

	cd "${destination}" || exit_with_error "can't change directory"

	cat <<- END > DEBIAN/control
		Package: quasar-led-screen
		Version: ${artifact_version}
		Architecture: ${ARCH}
		Maintainer: $MAINTAINER <$MAINTAINERMAIL>
		Section: universe/utils
		Priority: optional
		Description: Quasar 25x16 monochrome LED screen test tool
		 Test program for Quasar LED screen via SPI interface.
		END

	display_alert "Compiling quasar-led-screen" "CC=${KERNEL_COMPILER}gcc" "info"
	declare -g orig_dir="${SRC}/packages/quasar-led-screen"

	run_host_command_logged rm -f "${orig_dir}"/quasar_led_test
	run_host_command_logged make -C "${orig_dir}" CC="${KERNEL_COMPILER}gcc" clean all

	run_host_command_logged cp "${orig_dir}/quasar_led_test" "${destination}/usr/bin/"

	find "${destination}" -print0 2> /dev/null | xargs -0r chown --no-dereference 0:0
	find "${destination}" ! -type l -print0 2> /dev/null | xargs -0r chmod 'go=rX,u+rw,a-s'

	dpkg_deb_build "${destination}" "quasar-led-screen"

	done_with_temp_dir "${cleanup_id}"

	display_alert "Done building quasar-led-screen package" "${destination}" "debug"
}

function reversion_quasar-led-screen_deb_contents() {
	if [[ "${1}" != "quasar-led-screen" ]]; then
		return 0
	fi
	cat <<- EOF >> "${control_file_new}"
		Depends: libc6
	EOF
	return 0
}