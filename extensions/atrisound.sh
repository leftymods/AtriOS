# shellcheck shell=bash
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
	cat <<- 'UCM_MAIN' > "${destination}"/usr/share/alsa/ucm2/ATRISTATION/ATRISTATION.conf
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
	cat <<- 'UCM_HIFI' > "${destination}"/usr/share/alsa/ucm2/ATRISTATION/HiFi.conf
		SectionVerb {
			EnableSequence [
				cset "name='Tweeters Master Playback Switch' on"
				cset "name='Woofer Master Playback Switch' on"
				cset "name='Playback Switch' on"
				cset "name='Tweeters Master Playback Volume' 200"
				cset "name='Woofer Master Playback Volume' 200"
				cset "name='Playback Volume' 220"
			]
			DisableSequence [
			]
		}

		SectionDevice."Speaker" {
			Comment "Built-in stereo speakers (tweeters + woofer)"

			EnableSequence [
				cset "name='Tweeters Master Playback Switch' on"
				cset "name='Woofer Master Playback Switch' on"
				cset "name='Playback Switch' on"
				cset "name='Tweeters Master Playback Volume' 200"
				cset "name='Woofer Master Playback Volume' 200"
			]

			DisableSequence [
				cset "name='Tweeters Master Playback Switch' off"
				cset "name='Woofer Master Playback Switch' off"
				cset "name='Playback Switch' off"
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
	cat <<- 'UCM_VC' > "${destination}"/usr/share/alsa/ucm2/ATRISTATION/VoiceCall.conf
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
				cset "name='Tweeters Master Playback Switch' on"
				cset "name='Woofer Master Playback Switch' on"
				cset "name='Playback Switch' on"
				cset "name='Tweeters Master Playback Volume' 200"
				cset "name='Woofer Master Playback Volume' 200"
			]

			DisableSequence [
				cset "name='Tweeters Master Playback Switch' off"
				cset "name='Woofer Master Playback Switch' off"
				cset "name='Playback Switch' off"
				cset "name='Tweeters Master Playback Volume' 0"
				cset "name='Woofer Master Playback Volume' 0"
			]

			Value {
				PlaybackChannels "2"
			}
		}
	UCM_VC

	# Link YANDEX-STATION-MAX to ATRISTATION for UCM compatibility across boards
	ln -sfn ATRISTATION "${destination}"/usr/share/alsa/ucm2/YANDEX-STATION-MAX

	# ALSA default device -> ATRISTATION card (no UCM needed for
	# plain aplay/speaker-test)
	mkdir -pv "${destination}"/etc
	cat <<- 'ASOUND_CONF' > "${destination}"/etc/asound.conf
		pcm.!default {
		    type plug
		    slave.pcm "hw:ATRISTATION,0"
		}
		ctl.!default {
		    type hw
		    card "ATRISTATION"
		}
	ASOUND_CONF

	# Audio driver module auto-load (amplifiers, DAC and ADC)
	mkdir -pv "${destination}"/etc/modules-load.d
	cat <<- 'SOUND_MODS' > "${destination}"/etc/modules-load.d/sound.conf
		snd-soc-sy6045s
		snd-soc-es8156
		snd-soc-es7210
	SOUND_MODS

	# Install SY6045S I2C init script (DSP config for tweeters + woofer amps)
	run_host_command_logged mkdir -pv "${destination}"/usr/libexec
	cp "${SRC}/tools/audio/sy6045s-init.sh" "${destination}/usr/libexec/sy6045s-init.sh"
	run_host_command_logged chmod +x "${destination}"/usr/libexec/sy6045s-init.sh

	# Install SY6045S firmware settings for kernel driver (request_firmware)
	run_host_command_logged mkdir -pv "${destination}"/lib/firmware
	if [[ -f "${SRC}/tools/audio/sy6045s-tweeters-settings.txt" ]]; then
		cp "${SRC}/tools/audio/sy6045s-tweeters-settings.txt" "${destination}"/lib/firmware/
		cp "${SRC}/tools/audio/sy6045s-woofer-settings.txt" "${destination}"/lib/firmware/
		display_alert "SY6045S" "firmware settings installed" "info"
	else
		display_alert "SY6045S" "firmware settings not found in tools/audio/" "wrn"
	fi

	# Create systemd oneshot service for sound card init
	cat <<- 'SOUND_SERVICE' > "${destination}"/lib/systemd/system/atrisound.service
		[Unit]
		Description=AtriStation sound card initialization
		After=local-fs.target
		Before=alsa-restore.service
		DefaultDependencies=false

		[Service]
		Type=oneshot
		RemainAfterExit=yes
		NoNewPrivileges=yes
		# Wait for sound card device to appear
		ExecStart=/bin/sh -c 'i=0; while [ ! -e /dev/snd/pcmC0D0p ] && [ "$$i" -lt 20 ]; do sleep 0.2; i=$$((i+1)); done'
		# SY6045S: trigger firmware reload via sysfs, or run hardware init script as fallback
		ExecStart=/bin/sh -c 'if [ -d /sys/bus/i2c/drivers/sy6045s ]; then for d in /sys/bus/i2c/drivers/sy6045s/*; do [ -f "$$d/default_settings" ] && echo 1 > "$$d/default_settings" 2>/dev/null || true; done; elif [ -x /usr/libexec/sy6045s-init.sh ]; then /usr/libexec/sy6045s-init.sh || true; fi'
		# Unmute all output channels and set initial sensible volume
		ExecStart=/bin/sh -c 'amixer -c ATRISTATION sset "Tweeters Master" 75% unmute 2>/dev/null || true; amixer -c ATRISTATION sset "Woofer Master" 75% unmute 2>/dev/null || true; amixer -c ATRISTATION sset "Playback" 80% unmute 2>/dev/null || true'
		# Restore ALSA mixer state if saved
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
