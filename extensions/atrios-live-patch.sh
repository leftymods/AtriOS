function post_family_tweaks_bsp__atrios-live-patch() {

	display_alert "Extension: ${EXTENSION}: Installing AtriOS Live Patch" "${EXTENSION}" "info"

	run_host_command_logged cat <<- 'atrios-live-patch' > "${destination}"/etc/systemd/system/atrios-live-patch.service
		# AtriOS simple patch system service
		# Sometimes we need to fix minor issues like changing the key or fixing other small problem on live OS.
		# This downloads patch script from CDN, verify its signature and executes it at various stages
		#
		# Currently execute by: booting the system up, at apt upgrade stage, right before installing packages
		#
		# GH Action script for automatic signing and upload:
		# https://github.com/AtriOS/os/tree/main/live-patch

		[Unit]
		Description=AtriOS simple patch
		Wants=time-sync.target
		Before=time-sync.target
		After=network.target

		[Service]
		Type=forking
		ExecStart=/usr/lib/AtriOS/atrios-live-patch startup
		ExecStop=/usr/lib/AtriOS/atrios-live-patch stop
		RemainAfterExit=no
		TimeoutStartSec=2m

		[Install]
		WantedBy=multi-user.target
	atrios-live-patch

	run_host_command_logged cat <<- 'atrios-live-patching' > "${destination}"/usr/lib/AtriOS/atrios-live-patch
		#!/bin/bash
		#

		SERVER_PATH="https://dl.AtriOS.com/_patch"

		# exit if dependencies are not met
		if ! command -v "wget" &> /dev/null; then
				echo "Warning: patch system is not working as dependencies are not met (wget)"| logger -t "atrios-live-patch"
				exit 0
		fi

		if ! command -v gpg &> /dev/null; then
				echo "Warning: patch system is not working as dependencies are not met (gpg)"| logger -t "atrios-live-patch"
				exit 0
		fi

		case $1 in
			apt)
						PATCH="${SERVER_PATH}/01-pre-apt-upgrade.sh"
						PATCH_SIG="${SERVER_PATH}/01-pre-apt-upgrade.sh.asc"
				;;
				startup)
						PATCH="${SERVER_PATH}/02-startup.sh"
						PATCH_SIG="${SERVER_PATH}/02-startup.sh.asc"
				;;
			stop)
				exit 0
			;;
				*)
						echo "Warning: patch was not selected (apt|startup)"| logger -t "atrios-live-patch"
						exit 0
				;;
		esac

		echo "AtriOS live patch $1"

		TMP_DIR=$(mktemp -d -t test-XXXX)
		sleep 10
		timeout 10 wget -q --retry-connrefused --waitretry=3 --read-timeout=20 --timeout=15 -t 3 ${PATCH} -P ${TMP_DIR}
		timeout 10 wget -q --retry-connrefused --waitretry=3 --read-timeout=20 --timeout=15 -t 3 ${PATCH_SIG} -P ${TMP_DIR}

		# Check if installed key is ours
		export GNUPGHOME="${TMP_DIR}"
		gpg --keyring /usr/share/keyrings/AtriOS.gpg --list-keys 2>/dev/null | grep -q DF00FAF1C577104B50BF1D0093D6889F9F0E78D5
		if [[ $? != 0 ]]; then
			echo "Warning: signing key invalid or expired"| logger -t "atrios-live-patch"
		fi

		# Check if file is signed with AtriOS key
		gpg --keyring /usr/share/keyrings/AtriOS.gpg --verify ${TMP_DIR}/${PATCH_SIG##*/} ${TMP_DIR}/${PATCH##*/} > ${TMP_DIR}/live-patch.log 2>/dev/null

		if [[ $? == 0 ]]; then
				echo "Patch file is signed with AtriOS GPG key"
				echo "Running AtriOS Live Patch"
				bash ${TMP_DIR}/${PATCH##*/} | logger -t "atrios-live-patch"
				rm -rf ${TMP_DIR}
		else
				echo "Warning: we could not download patch files. Run manually: sudo bash $0 $1"| logger -t "atrios-live-patch"
		fi
		exit 0
	atrios-live-patching

	run_host_command_logged chmod -v 755 "${destination}"/usr/lib/AtriOS/atrios-live-patch

}
