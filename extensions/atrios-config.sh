# Install AtriOS config from repo. Now it is producing externally https://github.com/leftymods/configng
# and they are moved to main AtriOS repo periodically


function custom_apt_repo__add_AtriOS-github-repo() {
	cat <<- EOF > "${SDCARD}"/etc/apt/sources.list.d/atrios-config.sources
	Types: deb
	URIs: http://github.leftymods.com/configng
	Suites: stable
	Components: main
	Signed-By: ${APT_SIGNING_KEY_FILE}
	EOF
}


function post_AtriOS_repo_customize_image__install_atrios-config() {
	chroot_sdcard_apt_get_install "atrios-config"
}
