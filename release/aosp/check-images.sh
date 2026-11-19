#!/usr/bin/env bash
# Inspect AOSP output before using it as input to the Kobo runtime assembler.
set -euo pipefail
product=${1:?AOSP product output directory}
simg2img=${2:?AOSP host simg2img executable}
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT

bash "$(dirname "$0")/raw-system.sh" "$product/system.img" "$work/system.ext4" "$simg2img"
debugfs -R 'cat /build.prop' "$work/system.ext4" > "$work/build.prop"
grep -qx 'ro.build.version.sdk=24' "$work/build.prop"
grep -qx 'ro.product.cpu.abi=armeabi-v7a' "$work/build.prop"
grep -qx 'ro.product.name=aosp_arm' "$work/build.prop"

gzip -dc "$product/ramdisk.img" | cpio --quiet --extract --to-stdout init ./init > "$work/init"
cmp "$product/root/init" "$work/init"
readelf -h "$work/init" | grep -q 'Machine:.*ARM'
strings "$work/init" | grep -x 'KOBODROID_PROPERTY_LABEL_COMPAT' > /dev/null

sha256sum "$product/system.img" "$product/ramdisk.img"
printf 'AOSP_IMAGE_CHECK=PASS\n'
