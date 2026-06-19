#!/bin/bash
# AtriStation LED Ring Test Script
# Tests 24 RGB LEDs via leds-group-multicolor sysfs interface
#
# Usage:
#   sudo ./led-ring-test.sh          # run all tests
#   sudo ./led-ring-test.sh red      # all red
#   sudo ./led-ring-test.sh green    # all green
#   sudo ./led-ring-test.sh blue     # all blue
#   sudo ./led-ring-test.sh rainbow  # rainbow chase
#   sudo ./led-ring-test.sh off      # all off

LED_COUNT=24
SYSFS_BASE="/sys/class/leds"

# Color presets (R G B)
RED="255 0 0"
GREEN="0 255 0"
BLUE="0 0 255"
WHITE="255 255 255"
OFF="0 0 0"

set_led() {
	local num=$1
	local color=$2
	echo 255 > "${SYSFS_BASE}/ring${num}/brightness"
	echo "$color" > "${SYSFS_BASE}/ring${num}/multi_intensity"
}

set_all() {
	local color=$1
	for i in $(seq 0 $((LED_COUNT - 1))); do
		echo 255 > "${SYSFS_BASE}/ring${i}/brightness"
		echo "$color" > "${SYSFS_BASE}/ring${i}/multi_intensity"
	done
}

check_leds() {
	echo "=== Checking LED sysfs nodes ==="
	for i in $(seq 0 $((LED_COUNT - 1))); do
		if [[ -f "${SYSFS_BASE}/ring${i}/multi_intensity" ]]; then
			echo "  ring${i}: OK"
		else
			echo "  ring${i}: MISSING"
		fi
	done
}

test_individual() {
	echo "=== Individual LED Test ==="
	set_all "$OFF"
	sleep 0.2
	for i in $(seq 0 $((LED_COUNT - 1))); do
		set_led $i "$RED"
		sleep 0.05
		set_led $i "$OFF"
	done
	echo "Done: chased red across all LEDs"
}

test_colors() {
	echo "=== Color Test ==="
	for color in "$RED" "$GREEN" "$BLUE" "$WHITE"; do
		set_all "$color"
		sleep 0.5
	done
	set_all "$OFF"
	echo "Done: cycled red/green/blue/white"
}

test_rainbow() {
	echo "=== Rainbow Chase ==="
	set_all "$OFF"
	python3 -c "
import colorsys, os, time
leds = ['ring{i}/multi_intensity'.format(i=i) for i in range(24)]
base = '/sys/class/leds'
for step in range(0, 360, 5):
    for i in range(24):
        h = (step + i * 15) % 360
        r, g, b = colorsys.hsv_to_rgb(h / 360.0, 1.0, 1.0)
        with open(base + '/' + leds[i], 'w') as f:
            f.write('{r} {g} {b}'.format(r=int(r*255), g=int(g*255), b=int(b*255)))
        with open(base + '/ring{i}/brightness'.format(i=i), 'w') as f:
            f.write('255')
    time.sleep(0.05)
"
	set_all "$OFF"
	echo "Done: rainbow animation"
}

# Main
if [[ $EUID -ne 0 ]]; then
	echo "Error: must run as root (sudo)"
	exit 1
fi

case "${1:-all}" in
	red)
		set_all "$RED"
		;;
	green)
		set_all "$GREEN"
		;;
	blue)
		set_all "$BLUE"
		;;
	white)
		set_all "$WHITE"
		;;
	off)
		for i in $(seq 0 $((LED_COUNT - 1))); do
			echo 0 > "${SYSFS_BASE}/ring${i}/brightness"
		done
		;;
	check)
		check_leds
		;;
	individual)
		test_individual
		;;
	colors)
		test_colors
		;;
	rainbow)
		test_rainbow
		;;
	all|*)
		check_leds
		test_colors
		test_individual
		test_rainbow
		set_all "$OFF"
		echo "=== All tests complete ==="
		;;
esac
