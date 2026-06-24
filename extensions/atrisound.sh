#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026 leftymods
#
# AtriSound - audio subsystem initialization for AtriStation.
# Loads audio codec modules in correct order (matching Yandex vendor sequence)
# and waits for sound card to be ready before allowing audio services to start.

function post_family_tweaks_bsp__atrisound_add_config() {
	display_alert "Extension: ${EXTENSION}: ${BOARD}" "installing atrisound config" "info"
	: "${destination:?destination is not set}"

	# Create ALSA UCM2 config directory
	run_host_command_logged mkdir -pv "${destination}"/usr/share/alsa/ucm2/ATRISTATION

	# Write UCM config for ATRISTATION sound card
	cat <<- UCM_MAIN > "${destination}"/usr/share/alsa/ucm2/ATRISTATION/ATRISTATION.conf
		SectionUseCase."HiFi" {
			File "HiFi.conf"
			Comment "HiFi playback and capture"
		}

		SectionUseCase."VoiceCall" {
			File "VoiceCall.conf"
			Comment "Voice call with DMIC capture"
		}
	UCM_MAIN

	# Write HiFi UCM config
	cat <<- UCM_HIFI > "${destination}"/usr/share/alsa/ucm2/ATRISTATION/HiFi.conf
		SectionVerb {
			EnableSequence [
				cset "name='Tweeters Master Playback Volume' 200"
				cset "name='Woofer Master Playback Volume' 200"
			]
			DisableSequence [
			]
		}

		SectionDevice."Speaker" {
			Comment "Built-in stereo speakers (tweeters + woofer)"

			EnableSequence [
				cset "name='Tweeters Master Playback Volume' 200"
				cset "name='Woofer Master Playback Volume' 200"
			]

			DisableSequence [
				cset "name='Tweeters Master Playback Volume' 0"
				cset "name='Woofer Master Playback Volume' 0"
			]

			Value {
				PlaybackChannels "2"
			}
		}

		SectionDevice."DMICs" {
			Comment "Digital microphone array (4 channels)"

			EnableSequence [
			]

			DisableSequence [
			]

			Value {
				CaptureChannels "4"
			}
		}

		SectionDevice."HDMI" {
			Comment "HDMI audio output"

			EnableSequence [
			]

			DisableSequence [
			]

			Value {
				PlaybackChannels "8"
			}
		}
	UCM_HIFI

	# Write VoiceCall UCM config
	cat <<- UCM_VC > "${destination}"/usr/share/alsa/ucm2/ATRISTATION/VoiceCall.conf
		SectionVerb {
			EnableSequence [
			]
			DisableSequence [
			]
		}

		SectionDevice."DMICs" {
			Comment "Digital microphone array"

			EnableSequence [
			]

			DisableSequence [
			]

			Value {
				CaptureChannels "4"
			}
		}

		SectionDevice."Speaker" {
			Comment "Built-in speaker"

			EnableSequence [
				cset "name='Tweeters Master Playback Volume' 200"
				cset "name='Woofer Master Playback Volume' 200"
			]

			DisableSequence [
				cset "name='Tweeters Master Playback Volume' 0"
				cset "name='Woofer Master Playback Volume' 0"
			]

			Value {
				PlaybackChannels "2"
			}
		}
	UCM_VC

	# Create systemd oneshot service to wait for sound card
	cat <<- SOUND_SERVICE > "${destination}"/lib/systemd/system/atrisound.service
		[Unit]
		Description=AtriStation sound card initialization
		After=local-fs.target
		Before=alsa-restore.service
		DefaultDependencies=false

		[Service]
		Type=oneshot
		RemainAfterExit=yes
		ExecStart=/bin/sh -c 'i=0; while [ ! -e /dev/snd/pcmC0D0p ] && [ "$$i" -lt 20 ]; do sleep 0.2; i=$$((i+1)); done; /usr/sbin/alsactl init 0 || true'
		ExecStart=/usr/sbin/alsactl restore 0 || true

		[Install]
		WantedBy=multi-user.target
	SOUND_SERVICE

	return 0
}

function post_family_tweaks__atrisound_enable_service() {
	display_alert "Extension: ${EXTENSION}: ${BOARD}" "enabling atrisound service" "info"
	chroot_sdcard systemctl --no-reload enable "atrisound.service"
	return 0
}
