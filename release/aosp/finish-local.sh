#!/usr/bin/env bash
# Assemble and verify one local install candidate once both compile lanes pass.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
runtime=${1:?V73 host runtime directory}
extras_seed=${2:?Directory with source-built SwiftShader and probe.jar}
fbink=${3:?Path to supported FBInk binary}
output=${4:?Output installation ZIP}
context=${KOBO_DOCKER_CONTEXT:-colima-kobo-aosp}
aosp=kobodroid-aosp-integrated
webview=kobodroid-webview-build
webview_out=/chromium/src/out/Kobodroid
product=out/target/product/generic
kernel=${KOBO_LAB_KERNEL:-"$repo/../qemu-lab/vm/zImage"}
verification="$output.verification"

docker_local() { docker --context "$context" "$@"; }
require_pass() {
    test "$(docker_local inspect -f '{{.State.Status}} {{.State.ExitCode}}' "$1")" = 'exited 0'
}
require_pass "$aosp"
require_pass "$webview"
test ! -e "$output"
test ! -e "$output.sha256"
test -s "$kernel"
work=$(mktemp -d "$(dirname "$output")/.aosp-assembly.XXXXXXXX")
work=$(cd "$work" && pwd)
printf 'ASSEMBLY_DIRECTORY=%s\n' "$work"
docker_local cp "$webview:$webview_out/apks/SystemWebView.apk" "$work/webview.apk"
docker_local cp "$webview:$webview_out/Chromium-NOTICE.html" "$work/Chromium-NOTICE.html"
docker_local cp "$webview:$webview_out/WebView-target-deps.txt" "$work/WebView-target-deps.txt"
docker_local cp "$webview:/chromium/resolved-chromium-deps.txt" "$work/chromium-deps.txt"
mkdir -p "$work/container"
cp "$work/webview.apk" "$work/container/"
cp "$repo/release/webview/stage_apk.py" "$work/container/"
cp "$repo/release/aosp/check-images.sh" "$work/container/"
cp "$repo/release/aosp/raw-system.sh" "$work/container/"
cp "$repo/release/aosp/jack-env.sh" "$work/container/"
cat > "$work/container/rebuild.sh" <<'BUILD'
#!/usr/bin/env bash
set -eo pipefail
cd /aosp
python3 /tmp/replacement/stage_apk.py /aosp /tmp/replacement/webview.apk \
    --aapt /aosp/out/host/linux-x86/bin/aapt
source /tmp/replacement/jack-env.sh
source build/envsetup.sh
lunch aosp_arm-userdebug
make -j"${AOSP_JOBS:-4}" TARGET_USES_64_BIT_BINDER=true WITH_DEXPREOPT_BOOT_IMG_ONLY=true systemimage
bash /tmp/replacement/check-images.sh "$ANDROID_PRODUCT_OUT" "$ANDROID_HOST_OUT/bin/simg2img"
bash /tmp/replacement/raw-system.sh "$ANDROID_PRODUCT_OUT/system.img" /tmp/kobodroid-system.raw "$ANDROID_HOST_OUT/bin/simg2img"
e2fsck -fp /tmp/kobodroid-system.raw
resize2fs -M /tmp/kobodroid-system.raw
resize_check=0
e2fsck -fp /tmp/kobodroid-system.raw || resize_check=$?
test "$resize_check" = 0 || test "$resize_check" = 1
e2fsck -fn /tmp/kobodroid-system.raw
BUILD
final_container="kobodroid-aosp-with-webview-${work##*.}"
docker_local create --platform linux/amd64 --user builder --name "$final_container" \
    --mount source=kobodroid-aosp-r24,target=/aosp \
    kobodroid-aosp-nougat bash /tmp/replacement/rebuild.sh >/dev/null
docker_local cp "$work/container" "$final_container:/tmp/replacement"
docker_local start -a "$final_container"
require_pass "$final_container"

mkdir -p "$work/product/system/lib/hw" "$work/product/system/bin" "$work/product/system/etc" \
    "$work/app/launcher" "$work/app/overlay-framework" "$work/app/build/aosp-platform-key"
key="$work/app/build/aosp-platform-key"
docker_local cp "$final_container:/aosp/$product/root" "$work/product/root"
docker_local cp "$final_container:/aosp/$product/system/etc/init" "$work/product/system/etc/init"
for item in lib/hw/gralloc.kindle.so lib/libkdashmem.so lib/libkdnonwakealarm.so lib/libkdkill.so bin/kdhealthd; do
    docker_local cp "$final_container:/aosp/$product/system/$item" "$work/product/system/$item"
done
for item in platform.pk8 platform.x509.pem; do
    docker_local cp "$final_container:/aosp/build/target/product/security/$item" "$key/$item"
done
docker_local cp "$final_container:/aosp/$product/system/framework/framework-res.apk" "$key/fwres.apk"
docker_local cp "$final_container:/aosp/$product/system/etc/NOTICE.html.gz" "$work/NOTICE.html.gz"
docker_local cp "$final_container:/aosp/external/chromium-webview/prebuilt/arm/webview.apk" "$work/stock-webview.apk"
docker_local cp "$final_container:/aosp/resolved-manifest.xml" "$work/resolved-manifest.xml"
docker_local cp "$final_container:/tmp/kobodroid-system.raw" "$work/system.img"
cp -R "$repo/kobo-prototype/launcher/." "$work/app/launcher/"
cp -R "$repo/kobo-prototype/overlay-framework/." "$work/app/overlay-framework/"
sh "$work/app/launcher/build.sh"
sdk=${ANDROID_SDK:-"$HOME/Library/Android/sdk"}
expected=$(openssl x509 -in "$key/platform.x509.pem" -outform DER | shasum -a 256 | cut -d ' ' -f1)
for apk in KindleroidLauncher KobodroidFramework; do
    bundle="$work/app/build/launcher/$apk.apk"
    actual=$("$sdk/build-tools/35.0.1/apksigner" verify --min-sdk-version 24 --max-sdk-version 24 --print-certs "$bundle" |
        awk '/Signer #1 certificate SHA-256 digest:/ {print $NF}')
    test "$actual" = "$expected"
done
"${DEBUGFS:-debugfs}" -R "dump /app/webview/webview.apk $work/installed-webview.apk" "$work/system.img"
"$sdk/build-tools/35.0.1/zipalign" -c -p 4 "$work/installed-webview.apk"
"$sdk/build-tools/35.0.1/apksigner" verify --min-sdk-version 24 --max-sdk-version 24 "$work/installed-webview.apk"
mkdir "$work/extras"
cp "$extras_seed"/lib*swiftshader.so "$extras_seed/probe.jar" "$work/extras/"
cp "$work/app/build/launcher/KindleroidLauncher.apk" "$work/extras/launcher.apk"
cp "$work/app/build/launcher/KobodroidFramework.apk" "$work/extras/framework-overlay.apk"
sh "$repo/release/build-native.sh" "$work/native"

python3 "$repo/release/build-aosp-package.py" \
    --runtime "$runtime" --product "$work/product" --extras "$work/extras" \
    --native-tools "$work/native" \
    --system "$work/system.img" --manifest "$work/resolved-manifest.xml" \
    --webview "$work/webview.apk" --stock-webview "$work/stock-webview.apk" \
    --aosp-notice "$work/NOTICE.html.gz" --webview-notice "$work/Chromium-NOTICE.html" \
    --webview-revisions "$work/chromium-deps.txt" --webview-target-deps "$work/WebView-target-deps.txt" \
    --output "$work/kobodroid.zip" \
    --debugfs "${DEBUGFS:-debugfs}" --mke2fs "${MKE2FS:-mke2fs}"

mkdir -p "$work/extracted" "$work/volume/.kobo" "$work/volume/.adds/nm" "$work/volume/.adds/koreader"
unzip -q "$work/kobodroid.zip" -d "$work/extracted"
printf 'N428QA,4.9.77,4.45.23646\n' > "$work/volume/.kobo/version"
touch "$work/volume/.adds/nm/doc"
cp "$fbink" "$work/volume/.adds/koreader/fbink"
python3 "$work/extracted/install.py" "$work/volume"
(
    cd "$work/volume/.adds/kobodroid"
    shasum -a 256 -c checksums.sha256
    "${E2FSCK:-e2fsck}" -fn data.img
)
data="$work/volume/.adds/kobodroid/data.img"
printf 'upgrade-preserves-data\n' > "$work/upgrade-marker"
"${DEBUGFS:-debugfs}" -w -R "write $work/upgrade-marker /kobodroid-upgrade-check" "$data"
python3 "$work/extracted/install.py" "$work/volume"
test "$("${DEBUGFS:-debugfs}" -R 'cat /kobodroid-upgrade-check' "$data")" = upgrade-preserves-data
"${E2FSCK:-e2fsck}" -fn "$data"
echo AOSP_DESKTOP_INSTALL_QA=PASS

mkdir -p "$verification"
python3 "$repo/release/aosp/lab-boot.py" "$work/kobodroid.zip" "$kernel" "$verification/qemu.log" \
    --webview --sessions 2 --memory-mib 1024
source_asset="$verification/Kobodroid-v0.1.0-preview.1-source.zip"
python3 "$repo/release/export-source.py" "$source_asset"
(
    cd "$verification"
    source_file=$(basename "$source_asset")
    shasum -a 256 "$source_file" > "$source_file.sha256"
    shasum -a 256 -c "$source_file.sha256"
)
mv "$work/kobodroid.zip" "$output"
(
    cd "$(dirname "$output")"
    asset=$(basename "$output")
    shasum -a 256 "$asset" > "$asset.sha256"
    shasum -a 256 -c "$asset.sha256"
)
echo AOSP_DESKTOP_QA=PASS
