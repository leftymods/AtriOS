#!/system/bin/sh

get_chip_type()
{
  SDIO_DIR=/sys/bus/sdio/devices
  SDIO_VENDOR=$(cat $SDIO_DIR/*/vendor)
  SDIO_DEVICE=$(cat $SDIO_DIR/*/device)
  PCI_DIR=/sys/bus/pci/devices
  CHIP_TYPE="Unknown"

  if [ "$SDIO_VENDOR" = "0x024c" ] &&
     [ "$SDIO_DEVICE" = "0xa822" ] ; then
    CHIP_TYPE="Realtek"
  fi

  if [ "$SDIO_VENDOR" = "0x0000" ] &&
     [ "$SDIO_DEVICE" = "0x0000" ] ; then
    CHIP_TYPE="Unisoc"
  fi

  if grep -q 0x8086 $PCI_DIR/*/vendor && grep -q 0x095a $PCI_DIR/*/device; then
    CHIP_TYPE="Intel"
  fi
}

# script expects that /vendor/bin/wifi_power 1 is called externally

get_chip_type

case "$CHIP_TYPE" in
"Unisoc")
    echo Starting Unisoc Wi-Fi
    insmod /vendor/lib/modules/cfg80211.ko
    insmod /vendor/lib/modules/mac80211.ko
    insmod /vendor/lib/modules/uwe5621_bsp_sdio.ko
    insmod /vendor/lib/modules/sprdwl_ng.ko
    insmod /vendor/lib/modules/sprdbt_tty.ko
    ;;
"Realtek")
    echo Starting Realtek Wi-Fi
    insmod /vendor/lib/modules/cfg80211.ko
    insmod /vendor/lib/modules/mac80211.ko
    insmod /vendor/lib/modules/88x2es.ko
    ;;
"Intel")
    # XXX currently Intel is started from init.amlogic.board.rc
esac
