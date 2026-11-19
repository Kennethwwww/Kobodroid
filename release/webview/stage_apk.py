"""Stage a locally source-built ARM System WebView in the AOSP checkout."""
import argparse
from hashlib import sha256
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

MODULE = Path("external/chromium-webview")
LIBRARY = "lib/armeabi-v7a/libwebviewchromium.so"
ANCHOR = "LOCAL_SRC_FILES := prebuilt/$(my_src_arch)/webview.apk"
REPLACEMENT = "LOCAL_SRC_FILES := kobodroid-source/webview.apk"


def library_digest(apk):
    with zipfile.ZipFile(apk) as archive:
        with archive.open(LIBRARY) as file:
            header = file.read(20)
            if header[:6] != b"\x7fELF\x01\x01" or header[18:20] != b"\x28\x00":
                raise ValueError(f"Expected a little-endian ARM ELF32 library in {apk}")
            digest = sha256(header)
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                digest.update(chunk)
            return digest.hexdigest()


def stage(aosp, apk, aapt):
    module = aosp / MODULE
    build = module / "Android.mk"
    text = build.read_text()
    if text.count(ANCHOR) != 1 and text.count(REPLACEMENT) != 1:
        raise ValueError("Unknown AOSP WebView module")
    info = subprocess.check_output([aapt, "dump", "badging", str(apk)], universal_newlines=True)
    if not re.search(r"^package: name='com\.android\.webview'", info, re.M):
        raise ValueError("Expected com.android.webview")
    minimum = re.search(r"^sdkVersion:'(\d+)'", info, re.M)
    if minimum is None or int(minimum[1]) > 24:
        raise ValueError("WebView must support Android API 24")
    if library_digest(apk) == library_digest(module / "prebuilt/arm/webview.apk"):
        raise ValueError("The APK contains the original AOSP prebuilt WebView library")
    target = module / "kobodroid-source/webview.apk"
    target.parent.mkdir(exist_ok=True)
    shutil.copyfile(apk, target)
    build.write_text(text.replace(ANCHOR, REPLACEMENT))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("aosp", type=Path)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--aapt", default="aapt")
    args = parser.parse_args()
    stage(args.aosp, args.apk, args.aapt)
