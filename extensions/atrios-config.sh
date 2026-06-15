# AtriOS config extension placeholder.
# The original armbian-config (now atrios-config) was fetched from an external repo.
# Until a custom configng repo is ready, this extension does nothing.

function custom_apt_repo__add_atrios_github_repo() {
	: # no-op: atrios-config apt repo not configured yet
}

function post_atrios_repo_customize_image__install_atrios_config() {
	: # no-op: atrios-config package not available yet
}
