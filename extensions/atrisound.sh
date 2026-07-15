#
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2026 leftymods
#
# AtriSound - audio subsystem initialization for AtriStation.
# Configures two SY6045S amplifiers (tweeters + woofer) via I2C DSP init,
# loads codec modules in correct order, and waits for sound card readiness.
# Settings derived from Yandex Station (S905X2) firmware dump.

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

	# Install SY6045S I2C init script (DSP config for tweeters + woofer amps)
	run_host_command_logged mkdir -pv "${destination}"/usr/libexec
	cp "${SRC}/tools/audio/sy6045s-init.sh" "${destination}/usr/libexec/sy6045s-init.sh"
	run_host_command_logged chmod +x "${destination}"/usr/libexec/sy6045s-init.sh

	# Install SY6045S firmware settings for kernel driver (request_firmware)
	run_host_command_logged mkdir -pv "${destination}"/lib/firmware
	if [[ -f "${SRC}/../linux-unifreq/sound/soc/codecs/sy6045s-tweeters-settings.txt" ]]; then
		cp "${SRC}/../linux-unifreq/sound/soc/codecs/sy6045s-tweeters-settings.txt" "${destination}/lib/firmware/"
		cp "${SRC}/../linux-unifreq/sound/soc/codecs/sy6045s-woofer-settings.txt" "${destination}/lib/firmware/"
		display_alert "SY6045S" "firmware settings installed" "info"
	fi

	# Create systemd oneshot service for sound card init
	cat <<- SOUND_SERVICE > "${destination}"/lib/systemd/system/atrisound.service
		[Unit]
		Description=AtriStation sound card initialization
		After=local-fs.target
		Before=alsa-restore.service
		DefaultDependencies=false

		[Service]
		Type=oneshot
		RemainAfterExit=yes
		# Wait for sound card device to appear
		ExecStart=/bin/sh -c 'i=0; while [ ! -e /dev/snd/pcmC0D0p ] && [ "$$i" -lt 20 ]; do sleep 0.2; i=$$((i+1)); done'
		# Initialize SY6045S DSP config via I2C (EQ, DRC, volume curves)
		ExecStart=/usr/libexec/sy6045s-init.sh
		# Restore ALSA mixer state
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
