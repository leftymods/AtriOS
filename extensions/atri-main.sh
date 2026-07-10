#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025-2026 leftymods
#

# Build and install the atri-main userspace daemon package.
# atri-main replaces the cloud-dependent Yandex Station 2 maind.

function extension_prepare_config__atri_main() {
	# Nothing to check; package is always built for AtriStation
	return 0
}

function post_install_kernel_debs__atri_main_build_and_install() {
	: "${SDCARD:?SDCARD is not set}"
	: "${SRC:?SRC is not set}"

	local pkgdir="${SRC}/packages/atri-main"
	local pkgname="atri-main"
	local pkgversion="0.1"
	local arch="${ARCH}"          # target architecture from Armbian build
	local debfile="${pkgname}_${pkgversion}_${arch}.deb"
	local debs_dir="${SRC}/output/debs"

	display_alert "Extension: ${EXTENSION}: ${BOARD}" "building ${debfile}" "info"

	mkdir -p "${debs_dir}"

	# Build the binary for the target architecture.
	# The Makefile respects CC; use the Armbian cross compiler if available.
	local build_cc="${CC:-${CROSS_COMPILE}gcc}"
	if [[ -z "${build_cc}" || "${build_cc}" == "gcc" ]]; then
		build_cc="aarch64-linux-gnu-gcc"
	fi

	run_host_command_logged make -C "${pkgdir}" clean
	run_host_command_logged make -C "${pkgdir}" CC="${build_cc}" \
		|| exit_with_error "Extension: ${EXTENSION}: failed to build atri-main binary"

	# Stage the package contents manually and build the .deb with dpkg-deb.
	local stage="${pkgdir}/debian/${pkgname}"
	rm -rf "${stage}"
	mkdir -p "${stage}"

	run_host_command_logged make -C "${pkgdir}" install \
		DESTDIR="${stage}" PREFIX=/usr \
		|| exit_with_error "Extension: ${EXTENSION}: failed to stage atri-main"

	mkdir -p "${stage}/DEBIAN"
	cat > "${stage}/DEBIAN/control" << EOF
Package: ${pkgname}
Version: ${pkgversion}
Section: utils
Priority: optional
Architecture: ${arch}
Maintainer: leftymods <ggalab33@gmail.com>
Depends: ${shlibs:Depends}${shlibs:Depends:+, }systemd
Description: Offline userspace daemon for AtriStation
 atri-main drives the LED matrix framebuffer, RGB ring LEDs,
 input events and offline animations. It replaces the cloud
 dependent maind from the Yandex Station 2 firmware.
EOF

	cat > "${stage}/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
systemctl daemon-reload || true
systemctl enable atri-main.service || true
EOF
	chmod 755 "${stage}/DEBIAN/postinst"

	cat > "${stage}/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e
systemctl stop atri-main.service || true
systemctl disable atri-main.service || true
EOF
	chmod 755 "${stage}/DEBIAN/prerm"

	run_host_command_logged dpkg-deb --build "${stage}" "${debs_dir}/${debfile}" \
		|| exit_with_error "Extension: ${EXTENSION}: dpkg-deb failed"

	# Install the package inside the chroot image.
	cp "${debs_dir}/${debfile}" "${SDCARD}/root/"
	chroot_sdcard "DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /root/${debfile}" \
		|| exit_with_error "Extension: ${EXTENSION}: apt install failed for ${debfile}"
	rm -f "${SDCARD}/root/${debfile}"

	# Start on first boot.
	chroot_sdcard "systemctl --no-reload enable atri-main.service" || true

	display_alert "Extension: ${EXTENSION}: ${BOARD}" "installed ${debfile}" "info"
	return 0
}
