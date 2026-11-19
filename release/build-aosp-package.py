#!/usr/bin/env python3
"""Package a verified AOSP build for the Kobodroid Libra Colour installer."""
import argparse
import importlib.util
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from xml.etree import ElementTree

HERE = Path(__file__).resolve().parent
REVISION = "f6fcaed63cb61e2eeff44a3268ac6fbaaa2a8f88"
NATIVE_REPLACEMENTS = ("fs-probe", "installer-probe", "run-limit", "runner", "system-probe")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_inputs(options, work):
    manifest = ElementTree.parse(options.manifest).getroot()
    projects = manifest.findall("project")
    if not projects or any(not re.fullmatch(r"[0-9a-f]{40}", p.get("revision", "")) for p in projects):
        raise ValueError("Expected a resolved, revision-pinned AOSP manifest")
    if not any(p.get("path") == "frameworks/base" for p in projects):
        raise ValueError("Missing framework source revision")
    if not options.webview_revisions.is_file() or REVISION not in options.webview_revisions.read_text():
        raise ValueError("Expected Chromium source and dependency revisions")

    image = options.system.resolve()
    props = subprocess.check_output(
        [options.debugfs, "-R", "cat /build.prop", str(image)], universal_newlines=True
    ).splitlines()
    required = {"ro.build.version.sdk=24", "ro.product.cpu.abi=armeabi-v7a", "ro.product.name=aosp_arm"}
    if not required <= set(props):
        raise ValueError("Expected raw ARM AOSP API 24 ext4 system image")
    subprocess.run([options.debugfs, "-R", "dump /app/webview/webview.apk installed-webview.apk",
                    str(image)], cwd=work, check=True)
    apk = load("stage_apk", HERE / "webview/stage_apk.py")
    built = apk.library_digest(options.webview)
    if built != apk.library_digest(work / "installed-webview.apk"):
        raise ValueError("System image does not contain the selected source-built WebView")
    if built == apk.library_digest(options.stock_webview):
        raise ValueError("Original AOSP prebuilt WebView is not a release asset")


def build(options):
    if options.output.exists():
        raise ValueError("Choose a new output filename")
    original = load("build_release", HERE / "build-release.py")
    with tempfile.TemporaryDirectory(prefix="kobodroid-aosp-package-", dir=options.output.parent) as temp:
        work = Path(temp)
        check_inputs(options, work)
        files = {name: options.runtime / name for name in original.FILES
                 if name not in {"services-offline.jar", "libGLES_kindleroid.so", "root.tar.gz",
                                 "libkdkill.so", "launcher.apk", "framework-overlay.apk", "probe.jar"}}
        files["system.img"] = options.system
        for name in ("launcher.apk", "framework-overlay.apk", "probe.jar"):
            files[name] = options.extras / name
        for name in NATIVE_REPLACEMENTS:
            replacement = options.native_tools / name
            if b"GLIBC_PRIVATE" in replacement.read_bytes():
                raise ValueError(f"Expected the musl-built {name}")
            files[name] = replacement
        files["libkdkill.so"] = options.product / "system/lib/libkdkill.so"
        files["root.tar.gz"] = work / "root.tar.gz"
        root_builder = load("build_root", HERE / "aosp/build-root.py")
        root_builder.build(options.product, options.extras, files["root.tar.gz"])
        configure = load("adapt_setup", HERE / "aosp/adapt_setup.py")
        configure.adapt(files["setup.sh"], work / "setup.sh")
        files["setup.sh"] = work / "setup.sh"
        launch = files["launch.sh"].read_text()
        if launch.count("KOBODROID_V73") != 1:
            raise ValueError("Expected the V73 host runtime sources")
        (work / "launch.sh").write_text(launch.replace("KOBODROID_V73", "KOBODROID_V74_AOSP"))
        files["launch.sh"] = work / "launch.sh"
        notice_name = "AOSP-" + options.aosp_notice.name
        staged = work / "candidate.zip"
        original.archive_release(files, options.mke2fs, staged, "V74-AOSP", {
            "AOSP-RUNTIME.md": HERE.parent / "docs/AOSP-RUNTIME.md",
            f"notices/{notice_name}": options.aosp_notice,
            "notices/Chromium-NOTICE.html": options.webview_notice,
            "notices/AOSP-resolved-manifest.xml": options.manifest,
            "notices/Chromium-resolved-deps.txt": options.webview_revisions,
            "notices/Chromium-WebView-target-deps.txt": options.webview_target_deps,
            "notices/Chromium-args.gn": HERE / "webview/args.gn",
        })
        shutil.move(staged, options.output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "product", "extras", "native-tools", "system", "manifest", "webview",
                 "stock-webview", "aosp-notice", "webview-notice", "webview-revisions",
                 "webview-target-deps", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--debugfs", default="debugfs")
    parser.add_argument("--mke2fs", default="mke2fs")
    try:
        build(parser.parse_args())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
