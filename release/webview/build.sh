#!/usr/bin/env bash
# Run after gclient sync and Chromium's Android build dependencies are available.
set -euo pipefail
kit=$(cd "$(dirname "$0")" && pwd)
cd "${1:?Chromium src checkout}"
revision=f6fcaed63cb61e2eeff44a3268ac6fbaaa2a8f88
test "$(git rev-parse HEAD)" = "$revision"
python3 "$kit/patch_widevine.py" "$PWD"
mkdir -p out/Kobodroid
cp "$kit/args.gn" out/Kobodroid/args.gn
gn gen out/Kobodroid
gn desc out/Kobodroid //android_webview:system_webview_apk deps --all \
    > out/Kobodroid/WebView-target-deps.txt
autoninja -C out/Kobodroid -j "${WEBVIEW_JOBS:-2}" system_webview_apk
python3 tools/licenses/licenses.py --target-os android --gn-out-dir out/Kobodroid \
    --gn-target //android_webview:system_webview_apk credits out/Kobodroid/Chromium-NOTICE.html
test -s out/Kobodroid/apks/SystemWebView.apk
test -s out/Kobodroid/Chromium-NOTICE.html
