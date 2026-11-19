#!/usr/bin/env bash
# Run inside the isolated x86_64 Linux builder.
set -euo pipefail
cd /aosp
tag=android-7.0.0_r24
manifest=fda0297eb5fe125c22119e7d0f809e9b63a99ee6
jobs=${AOSP_JOBS:-4}
if [[ ${1:-} == sync ]]; then
    git config --global http.version HTTP/1.1
    repo init -u https://android.googlesource.com/platform/manifest -b "refs/tags/$tag" \
        --depth=1 --no-clone-bundle
    test "$(git -C .repo/manifests rev-parse HEAD)" = "$manifest"
    repo sync -c --no-tags --no-clone-bundle --fail-fast -j"$jobs"
    repo manifest -r -o resolved-manifest.xml
    exit
fi
test "$(git -C .repo/manifests rev-parse HEAD)" = "$manifest"
test -s resolved-manifest.xml
python3 /opt/kobodroid/aosp/patch_init.py /aosp
python3 /opt/kobodroid/aosp/patch_art_host.py /aosp
source /opt/kobodroid/aosp/jack-env.sh
# Nougat envsetup and make expect to read optional undefined shell variables.
set +u
source build/envsetup.sh
lunch aosp_arm-userdebug
make -j"$jobs" TARGET_USES_64_BIT_BINDER=true WITH_DEXPREOPT_BOOT_IMG_ONLY=true systemimage ramdisk simg2img
/opt/kobodroid/aosp/check-images.sh "$ANDROID_PRODUCT_OUT" "$ANDROID_HOST_OUT/bin/simg2img"
