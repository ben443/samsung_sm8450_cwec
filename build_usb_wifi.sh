#!/bin/bash

set -e

KERNEL_DIR=${KERNEL_DIR:-./msm-kernel}

if [ ! -d "$KERNEL_DIR" ]; then
    echo "Error: kernel directory $KERNEL_DIR not found."
    exit 1
fi

WIFI_DIR="$KERNEL_DIR/drivers/net/wireless"
if [ ! -d "$WIFI_DIR" ]; then
    echo "Error: wireless drivers directory $WIFI_DIR not found."
    exit 1
fi

export ARCH=${ARCH:-arm64}
export CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}

# Configure the kernel
echo "Configuring kernel..."
make -C "$KERNEL_DIR" defconfig

# Find USB WiFi driver Kconfigs and extract their CONFIG_ names
echo "Enabling USB WiFi modules in .config..."
# Specifically target config blocks in Kconfig files within drivers/net/wireless that depend on USB
CONFIGS=$(awk '/^config / {conf=$2} /depends on.*USB/ {print "CONFIG_"conf}' $(find "$WIFI_DIR" -name Kconfig) | sort -u)

# Also enable basic wireless support if not already
CONFIGS="$CONFIGS CONFIG_WLAN=y CONFIG_CFG80211=m CONFIG_MAC80211=m CONFIG_USB=y CONFIG_USB_NET_DRIVERS=y"

for conf in $CONFIGS; do
    # Handle both =y and =m in our list
    name=${conf%=*}
    val=${conf#*=}
    if [ "$name" == "$conf" ]; then
        val="m"
    fi

    sed -i "s/.*$name.*/${name}=${val}/g" "$KERNEL_DIR/.config"
    if ! grep -q "^${name}=${val}$" "$KERNEL_DIR/.config"; then
        echo "${name}=${val}" >> "$KERNEL_DIR/.config"
    fi
done

# Normalize the configuration to resolve dependencies automatically
make -C "$KERNEL_DIR" olddefconfig

# Prepare modules
make -C "$KERNEL_DIR" modules_prepare

# Build the modules using standard kernel build
echo "Building all modules..."
make -C "$KERNEL_DIR" modules

echo "Extracting USB WiFi modules..."
mkdir -p usb_wifi_modules
# Since we enabled them, they will be built inside their respective dirs.
# Find .ko files specifically in wireless directory.
find "$WIFI_DIR" -name "*.ko" -exec cp {} usb_wifi_modules/ \;