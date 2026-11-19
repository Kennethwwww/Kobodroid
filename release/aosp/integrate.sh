#!/usr/bin/env bash
# Run after the baseline AOSP build completes. Build the Kobo source changes
# and native modules against the same ARM system and ABI.
set -eo pipefail
kit=$(cd "$(dirname "$0")" && pwd)
cd /aosp
test -s resolved-manifest.xml
test -s out/target/product/generic/system.img
test -s out/target/product/generic/ramdisk.img
python3 "$kit/patch_framework.py" /aosp
python3 "$kit/stage_network.py" /aosp
python3 "$kit/stage_native.py" /aosp
source "$kit/jack-env.sh"
source build/envsetup.sh
lunch aosp_arm-userdebug
make -j"${AOSP_JOBS:-4}" TARGET_USES_64_BIT_BINDER=true WITH_DEXPREOPT_BOOT_IMG_ONLY=true \
    systemimage ramdisk simg2img gralloc.kindle libGLES_kindleroid kdhealthd \
    libkdashmem libkdnonwakealarm libkdkill
"$kit/check-images.sh" "$ANDROID_PRODUCT_OUT" "$ANDROID_HOST_OUT/bin/simg2img"
for built in lib/hw/gralloc.kindle.so lib/libGLES_kindleroid.so bin/kdhealthd lib/libkdashmem.so \
             lib/libkdnonwakealarm.so lib/libkdkill.so; do
    readelf -h "$ANDROID_PRODUCT_OUT/system/$built" | grep 'Machine:.*ARM'
done
