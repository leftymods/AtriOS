setenv scriptaddr "0x32000000"
setenv kernel_addr_r "0x34000000"
setenv fdt_addr_r "0x4080000"
setenv overlay_error "false"
setenv rootdev "/dev/mmcblk1p1"
setenv verbosity "7"
setenv console "both"
setenv bootlogo "false"
setenv rootfstype "ext4"
setenv docker_optimizations "on"
setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
setenv dtb_loadaddr "0x1000000"
setenv k_addr "0x1100000"
setenv loadaddr "0x1B00000"
setenv initrd_loadaddr "0x4080000"
setenv display_autodetect "true"
setenv hdmimode "1080p60hz"
setenv monitor_onoff "false"
setenv overscan "100"
setenv sdrmode "auto"
setenv voutmode "hdmi"
setenv disablehpd "false"
setenv cec "false"
setenv disable_vu7 "true"
setenv max_freq_a55 "1908"
setenv maxcpus "4"
echo "U-boot default fdtfile: ${fdtfile}"
echo "Current variant: ${variant}"
if test "x${variant}" = "xn2_plus"; then
	setenv fdtfile "amlogic/meson-g12b-odroid-n2-plus.dtb"
	echo "For variant ${variant}, set default fdtfile: ${fdtfile}"
fi
if test "x${variant}" = "xn2-plus"; then
	setenv fdtfile "amlogic/meson-g12b-odroid-n2-plus.dtb"
	echo "For variant ${variant} (dash version, 2021.07 or up), set default fdtfile: ${fdtfile}"
fi
if test "x${fdtfile}" = "xamlogic/meson-sm1-odroid-.dtb"; then
	setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
	echo "AtriStation fallback: setting default fdtfile to ${fdtfile}"
fi
if test "x${fdtfile}" = "xmeson-sm1-odroid-.dtb"; then
	setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
	echo "AtriStation fallback: setting default fdtfile to ${fdtfile}"
fi
if test "x${fdtfile}" = "x"; then
	setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
	echo "AtriStation fallback: setting default fdtfile to ${fdtfile}"
fi
if test -e ${devtype} ${devnum} ${prefix}atriosEnv.txt; then
	load ${devtype} ${devnum} ${scriptaddr} ${prefix}atriosEnv.txt
	env import -t ${scriptaddr} ${filesize}
fi
if test -e ${devtype} ${devnum} ${prefix}armbianEnv.txt; then
	load ${devtype} ${devnum} ${scriptaddr} ${prefix}armbianEnv.txt
	env import -t ${scriptaddr} ${filesize}
fi
if test "x${devtype}" = "xmmc"; then part uuid mmc ${devnum}:1 partuuid; fi
if test "x${console}" = "xdisplay"; then setenv consoleargs "console=tty1"; fi
if test "x${fdtfile}" = "xamlogic/meson-sm1-odroid-.dtb"; then
	setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
fi
if test "x${fdtfile}" = "xmeson-sm1-odroid-.dtb"; then
	setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
fi
if test "x${fdtfile}" = "x"; then
	setenv fdtfile "amlogic/meson-sm1-atristation.dtb"
fi
echo "Current fdtfile after atriosEnv: ${fdtfile}"
if test -e ${devtype} ${devnum} ${prefix}zImage; then
	if test "x${console}" = "xserial"; then setenv consoleargs "earlycon console=ttyS0,115200"; fi
	if test "x${console}" = "xdisplay"; then setenv consoleargs "earlycon console=tty1"; fi
	if test "x${console}" = "xboth"; then setenv consoleargs "earlycon console=tty1 console=ttyS0,115200"; fi
	if test "x${bootlogo}" = "xtrue"; then
		setenv consoleargs "splash plymouth.ignore-serial-consoles ${consoleargs}"
	fi
	setenv bootargs "root=${rootdev} rootwait rootfstype=${rootfstype} ${consoleargs} consoleblank=0 coherent_pool=2M loglevel=${verbosity} ${amlogic} no_console_suspend fsck.repair=yes net.ifnames=0 elevator=noop hdmimode=${hdmimode} cvbsmode=576cvbs max_freq_a55=${max_freq_a55} maxcpus=${maxcpus} voutmode=${voutmode} ${cmode} disablehpd=${disablehpd} cvbscable=${cvbscable} overscan=${overscan} ${hid_quirks} monitor_onoff=${monitor_onoff} ${cec_enable} sdrmode=${sdrmode}"
	echo "Legacy bootargs: ${bootargs}"
	load ${devtype} ${devnum} ${k_addr} boot/zImage
	load ${devtype} ${devnum} ${dtb_loadaddr} boot/dtb/${fdtfile}
	load ${devtype} ${devnum} ${initrd_loadaddr} boot/uInitrd
	fdt addr ${dtb_loadaddr}
	unzip ${k_addr} ${loadaddr}
	booti ${loadaddr} ${initrd_loadaddr} ${dtb_loadaddr}
else
	if test "x${console}" = "xserial"; then setenv consoleargs "earlycon console=ttyAML0,115200"; fi
	if test "x${console}" = "xdisplay"; then setenv consoleargs "earlycon console=tty1"; fi
	if test "x${console}" = "xboth"; then setenv consoleargs "earlycon console=tty1 console=ttyAML0,115200"; fi
	if test "x${bootlogo}" = "xtrue"; then
		setenv consoleargs "splash plymouth.ignore-serial-consoles ${consoleargs}"
	fi
	if test "x${disable_vu7}" = "xfalse"; then setenv usbhidquirks "usbhid.quirks=0x0eef:0x0005:0x0004"; fi
	setenv bootargs "root=${rootdev} rootwait rootfstype=${rootfstype} ${consoleargs} consoleblank=0 coherent_pool=2M loglevel=${verbosity} ubootpart=${partuuid} libata.force=noncq usb-storage.quirks=${usbstoragequirks} ${usbhidquirks} ${extraargs} ${extraboardargs}"
	if test "x${docker_optimizations}" = "xon"; then setenv bootargs "${bootargs} cgroup_enable=memory"; fi
	echo "Mainline bootargs: ${bootargs}"
	if test "x${bootlogo}" = "xtrue"; then
		if load ${devtype} ${devnum} ${kernel_addr_r} ${prefix}boot.bmp; then
			bmp display ${kernel_addr_r}
		fi
	fi
	load ${devtype} ${devnum} ${ramdisk_addr_r} ${prefix}uInitrd
	load ${devtype} ${devnum} ${kernel_addr_r} ${prefix}Image
	load ${devtype} ${devnum} ${fdt_addr_r} ${prefix}dtb/${fdtfile}
	fdt addr ${fdt_addr_r}
	fdt resize 65536
	for overlay_file in ${overlays}; do
		if load ${devtype} ${devnum} ${scriptaddr} ${prefix}dtb/amlogic/overlay/${overlay_prefix}-${overlay_file}.dtbo; then
			echo "Applying kernel provided DT overlay ${overlay_prefix}-${overlay_file}.dtbo"
			fdt apply ${scriptaddr} || setenv overlay_error "true"
		fi
	done
	for overlay_file in ${user_overlays}; do
		if load ${devtype} ${devnum} ${scriptaddr} ${prefix}overlay-user/${overlay_file}.dtbo; then
			echo "Applying user provided DT overlay ${overlay_file}.dtbo"
			fdt apply ${scriptaddr} || setenv overlay_error "true"
		fi
	done
	if test "x${overlay_error}" = "xtrue"; then
		echo "Error applying DT overlays, restoring original DT"
		load ${devtype} ${devnum} ${fdt_addr_r} ${prefix}dtb/${fdtfile}
	else
		if load ${devtype} ${devnum} ${scriptaddr} ${prefix}dtb/amlogic/overlay/${overlay_prefix}-fixup.scr; then
			echo "Applying kernel provided DT fixup script (${overlay_prefix}-fixup.scr)"
			source ${scriptaddr}
		fi
		if test -e ${devtype} ${devnum} ${prefix}fixup.scr; then
			load ${devtype} ${devnum} ${scriptaddr} ${prefix}fixup.scr
			echo "Applying user provided fixup script (fixup.scr)"
			source ${scriptaddr}
		fi
	fi
	booti ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}
fi
