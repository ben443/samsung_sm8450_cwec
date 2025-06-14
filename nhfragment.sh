#!/bin/bash
# setup_nethunter_gki.sh: GKI NetHunter config setup for SM8450

set -e

FRAGMENT=sm8450_nethunter.fragment
MODULES_BZL=common/modules.bzl

# List of WiFi adapter modules to add
WIFI_MODULES=(
    "drivers/net/wireless/realtek/rtl8187/rtl8187.ko"
    "drivers/net/wireless/realtek/rtl8xxxu/rtl8xxxu.ko"
    "drivers/net/wireless/realtek/rtlwifi/rtl8192cu/rtl8192cu.ko"
    "drivers/net/wireless/ath/ath9k/ath9k_htc.ko"
    "drivers/net/wireless/mediatek/mt7601u/mt7601u.ko"
    "drivers/net/wireless/realtek/rtl8812au/rtl8812au.ko"
)

cat > $FRAGMENT <<EOF
# Basic NetHunter support (GKI modules)
CONFIG_SYSVIPC=y
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_MODULE_FORCE_UNLOAD=y
CONFIG_MODVERSIONS=y
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NETFILTER_XTABLES=y
CONFIG_USB_OTG=y
CONFIG_USB_HOST=y
CONFIG_USB_NET_DRIVERS=m
CONFIG_USB_SERIAL=m
CONFIG_USB_SERIAL_GENERIC=m
CONFIG_RTL8187=m
CONFIG_RTL8192CU=m
CONFIG_RTL8XXXU=m
CONFIG_ATH9K_HTC=m
CONFIG_MT7601U=m
CONFIG_RTL8812AU=m
CONFIG_WLAN_VENDOR_REALTEK=y
CONFIG_WLAN_VENDOR_ATH=y
CONFIG_WLAN_VENDOR_MEDIATEK=y
CONFIG_CFG80211=m
CONFIG_MAC80211=m
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_F_HID=y
CONFIG_USB_CONFIGFS_F_MASS_STORAGE=y
CONFIG_USB_CONFIGFS_F_RNDIS=y
CONFIG_HID=y
CONFIG_HID_GENERIC=y
CONFIG_USB_HID=y
EOF

echo "Merging config fragment with gki_defconfig..."
scripts/kconfig/merge_config.sh -m arch/arm64/configs/gki_defconfig $FRAGMENT

echo "Copying merged config to arch/arm64/configs/gki_defconfig..."
cp .config arch/arm64/configs/gki_defconfig

# Update modules.bzl if present
if [ -f "$MODULES_BZL" ]; then
    echo "Adding WiFi modules to $MODULES_BZL..."
    for mod in "${WIFI_MODULES[@]}"; do
        grep -q "$mod" "$MODULES_BZL" || \
        sed -i "/'kernel\/drivers\/.*'/a \    'kernel/${mod}'," "$MODULES_BZL"
    done
    echo "WiFi modules added to $MODULES_BZL."
else
    echo "Warning: $MODULES_BZL not found. Please add the following modules manually:"
    for mod in "${WIFI_MODULES[@]}"; do
        echo "  kernel/${mod}"
    done
fi

echo
echo "Next steps for GKI module compliance:"
echo "1. Build your kernel as usual (e.g., make ARCH=arm64 ...)."
echo "2. Ensure all .ko modules are built and present in out/arch/arm64/boot."
echo "3. Regenerate modules.order (make modules_install or appropriate build step)."
echo "4. Rebuild system_dlkm.img to include the new modules."
echo "5. Flash boot.img and system_dlkm.img to your device."
echo
echo "TIPS:"
echo "- For a successful boot, ensure:"
echo "  * Your device's vendor_dlkm and system_dlkm images are updated with the new modules."
echo "  * The boot image is signed if your device requires it."
echo "  * All module dependencies are satisfied and present in the module images."
echo
echo "Done. Your kernel source is now configured for GKI NetHunter and WiFi adapter support."
