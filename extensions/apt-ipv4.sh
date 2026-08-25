# shellcheck shell=bash
#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025-2026 leftymods
#
# Force apt to use IPv4. The host network has no working IPv6 route to
# deb.debian.org (connect hangs until timeout), so every apt run inside
# the build chroot spams "Tried to start delayed item ... failed" while
# falling back. Applies to the built image as well.

function post_family_tweaks_bsp__force_apt_ipv4() {
	display_alert "Extension: ${EXTENSION}: ${BOARD}" "forcing IPv4 for apt" "info"
	: "${destination:?destination is not set}"

	mkdir -pv "${destination}"/etc/apt/apt.conf.d
	cat <<- 'APT_IPV4_CONF' > "${destination}"/etc/apt/apt.conf.d/99-force-ipv4
		// deb.debian.org has no usable IPv6 path on AtriOS networks;
		// avoid multi-second v6 connect timeouts on every fetch
		Acquire::ForceIPv4 "true";
	APT_IPV4_CONF

	return 0
}
