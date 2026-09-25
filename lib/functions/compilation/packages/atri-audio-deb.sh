#!/usr/bin/env bash
#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025-2026 leftymods
#
# This file is a part of the AtriOS Build Framework
# https://github.com/leftymods/CoreOS/

compile_atri-audio() {
	: "${artifact_version:?artifact_version is not set}"

	declare cleanup_id="" tmp_dir=""
	prepare_temp_dir_in_workdir_and_schedule_cleanup "deb-atri-audio" cleanup_id tmp_dir

	declare atri_audio_dir="atri-audio"
	mkdir -p "${tmp_dir}/${atri_audio_dir}"

	declare destination="${tmp_dir}/${atri_audio_dir}"

	run_host_command_logged mkdir -p "${destination}"/{DEBIAN,usr/bin,lib/systemd/system,lib/firmware}

	cd "${destination}" || exit_with_error "can't change directory"

	cat <<- END > DEBIAN/control
		Package: atri-audio
		Version: ${artifact_version}
		Architecture: ${ARCH}
		Maintainer: $MAINTAINER <$MAINTAINERMAIL>
		Section: universe/sound
		Priority: optional
		Description: AtriOS audio testing tools and volume knob daemon
		 Provides atri-sound-test and atrivolume for AtriStation.
	END

	display_alert "Compiling atri-audio" "CC=${KERNEL_COMPILER}gcc" "info"
	declare -g orig_dir="${SRC}/packages/atri-audio"

	run_host_command_logged make -C "${orig_dir}" clean

	run_host_command_logged \
		make -C "${orig_dir}" \
		CC="${KERNEL_COMPILER}gcc" \
		DESTDIR="${destination}" \
		install

	run_host_command_logged cp "${orig_dir}"/debian/{postinst,prerm} "${destination}"/DEBIAN/ 2>/dev/null || true
	chmod 755 "${destination}"/DEBIAN/{postinst,prerm} 2>/dev/null || true

	find "${destination}" -print0 2> /dev/null | xargs -0r chown --no-dereference 0:0
	find "${destination}" ! -type l -print0 2> /dev/null | xargs -0r chmod 'go=rX,u+rw,a-s'

	dpkg_deb_build "${destination}" "atri-audio"

	done_with_temp_dir "${cleanup_id}"

	display_alert "Done building atri-audio package" "${destination}" "debug"
}

function reversion_atri-audio_deb_contents() {
	if [[ "${1}" != "atri-audio" ]]; then
		return 0
	fi
	display_alert "Reversion" "reversion_atri-audio_deb_contents: '$*'" "debug"

	cat <<- EOF >> "${control_file_new}"
		Depends: libc6, libasound2
	EOF

	return 0
}
