#!/bin/sh
# Builds the Kobodroid launcher (kindleroid.launcher, platform-signed so its status bar window is allowed) and the
# framework overlay, with the local Android SDK. The platform key is AOSP's public test key, which signs the
# Android 7 test-keys system image itself.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
SDK=${ANDROID_SDK:-$HOME/Library/Android/sdk}
bt=$SDK/build-tools/35.0.1
jar=$SDK/platforms/android-31/android.jar
fwres=$here/../build/aosp-platform-key/fwres.apk
key=$here/../build/aosp-platform-key
out=$here/../build/launcher
rm -rf "$out" && mkdir -p "$out/classes" "$out/gen"
"$bt/aapt" package -f -m -J "$out/gen" -M "$here/AndroidManifest.xml" -S "$here/res" -I "$fwres"
javac --release 8 -encoding UTF-8 -Xlint:-options -nowarn -cp "$jar" -d "$out/classes" "$here"/src/kindleroid/launcher/*.java "$out"/gen/kindleroid/launcher/R.java
"$bt/d8" --min-api 24 --release --lib "$jar" --output "$out" $(find "$out/classes" -name '*.class')
"$bt/aapt" package -f -M "$here/AndroidManifest.xml" -A "$here/assets" -S "$here/res" -I "$fwres" -F "$out/unsigned.apk"
(cd "$out" && "$bt/aapt" add unsigned.apk classes.dex >/dev/null)
"$bt/zipalign" -f 4 "$out/unsigned.apk" "$out/aligned.apk"
"$bt/apksigner" sign --key "$key/platform.pk8" --cert "$key/platform.x509.pem" --min-sdk-version 23 \
    --out "$out/KindleroidLauncher.apk" "$out/aligned.apk"
overlay=$here/../overlay-framework
"$bt/aapt" package -f -M "$overlay/AndroidManifest.xml" -S "$overlay/res" -I "$fwres" -F "$out/overlay-unsigned.apk"
"$bt/zipalign" -f 4 "$out/overlay-unsigned.apk" "$out/overlay-aligned.apk"
"$bt/apksigner" sign --key "$key/platform.pk8" --cert "$key/platform.x509.pem" --min-sdk-version 23 \
    --out "$out/KobodroidFramework.apk" "$out/overlay-aligned.apk"
ls -l "$out/KindleroidLauncher.apk" "$out/KobodroidFramework.apk"
