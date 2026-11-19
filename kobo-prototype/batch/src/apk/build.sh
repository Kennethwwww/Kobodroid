#!/bin/sh
# Build kobo-probe.apk with the API 23 SDK in weread-jammy-cross (same toolchain as kindleroid-launcher).
# Usage (on the Mac): docker cp install-probe7 weread-jammy-cross:/tmp/kd-probe7
#                     docker exec weread-jammy-cross sh /tmp/kd-probe7/build.sh
set -eu
export PATH=/usr/lib/android-sdk/build-tools/debian:/usr/lib/jvm/java-11-openjdk-arm64/bin:$PATH
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk=/usr/lib/android-sdk
jar=$sdk/platforms/android-23/android.jar
out=$src/out
rm -rf "$out" && mkdir -p "$out/classes"
javac --release 7 -encoding UTF-8 -Xlint:-options -cp "$jar" -d "$out/classes" "$src"/src/kindleroid/koboprobe/*.java
$sdk/build-tools/debian/dx --dex --output="$out/classes.dex" "$out/classes"
aapt package -f -M "$src/AndroidManifest.xml" -I "$jar" -F "$out/unsigned.apk"
(cd "$out" && aapt add unsigned.apk classes.dex >/dev/null)
# Disposable test key, generated once and kept next to the sources.
key=$src/test-key.jks
[ -f "$key" ] || keytool -genkeypair -keystore "$key" -storepass kindleroid-test -keypass kindleroid-test \
    -alias sample -keyalg RSA -keysize 2048 -validity 3650 -dname 'CN=Kindleroid sample reader test key'
apksigner sign --ks "$key" --ks-key-alias sample --ks-pass pass:kindleroid-test --key-pass pass:kindleroid-test \
    --min-sdk-version 23 --out "$out/kobo-probe.apk" "$out/unsigned.apk"
apksigner verify --min-sdk-version 23 "$out/kobo-probe.apk"
ls -l "$out/kobo-probe.apk"
