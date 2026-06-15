# AtriOS Package Archive (APA) extension placeholder.
# The original APA repo is not available yet.

function extension_prepare_config__apa() {
	display_alert "APA extension loaded" "repo not configured yet" "info"
	export APA_IS_ACTIVE="false"
}

function custom_apt_repo__add_apa() {
	: # no-op: APA repo not available yet
}

function post_atrios_repo_customize_image__install_from_apa() {
	: # no-op: APA repo not available yet
}
