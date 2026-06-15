# Install AtriOS-common etc. from APA

function extension_prepare_config__apa() {
	display_alert "Target image will have AtriOS Package Archive (APA) enabled by default" "${EXTENSION}" "info"
	export APA_IS_ACTIVE="true"
}

function custom_apt_repo__add_apa() {
	run_host_command_logged echo "deb [signed-by=${APT_SIGNING_KEY_FILE}] http://github.leftymods.com/apa current main" "|" tee "${SDCARD}"/etc/apt/sources.list.d/AtriOS-apa.list
}

function post_AtriOS_repo_customize_image__install_from_apa() {
	# do not install AtriOS recommends for minimal images
	[[ "${BUILD_MINIMAL,,}" =~ ^(true|yes)$ ]] && INSTALL_RECOMMENDS="no-install-recommends" || INSTALL_RECOMMENDS="install-recommends"
	chroot_sdcard_apt_get install --$INSTALL_RECOMMENDS AtriOS-common AtriOS-bsp
	chroot_sdcard rm -f /etc/apt/sources.list.d/AtriOS-apa.list.inactive

	# install desktop environment if requested
	case ${DESKTOP_ENVIRONMENT^^} in
		XFCE|KDE|GNOME)
			display_alert "installing ${DESKTOP_ENVIRONMENT^^} desktop environment" "${EXTENSION}: ${DESKTOP_ENVIRONMENT^^}" "info"
			chroot_sdcard_apt_get install --install-recommends=yes "AtriOS-desktop-${DESKTOP_ENVIRONMENT,,}"
			;;
	esac
}
