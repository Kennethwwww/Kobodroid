#!/bin/sh
# Builds session-input.jar (a dex jar for app_process) with the local Android SDK.
set -eu
cd "$(dirname "$0")"
SDK=${ANDROID_SDK:-$HOME/Library/Android/sdk}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/classes"
javac --release 8 -Xlint:-options -cp "$SDK/platforms/android-31/android.jar" -d "$work/classes" SessionInput.java
"$SDK/build-tools/35.0.1/d8" --min-api 24 --release --lib "$SDK/platforms/android-31/android.jar" --output "$work" "$work"/classes/*.class
(cd "$work" && zip -q session-input.jar classes.dex)
cp "$work/session-input.jar" session-input.jar
