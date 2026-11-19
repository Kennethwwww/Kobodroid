#!/usr/bin/env python3
"""Assemble a local V73 installation candidate from verified runtime inputs."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from install import DATA_BYTES, digest

SYSTEM_SHA256 = "87c7ac4b466355da4ba11cd94b70f3b9ffd40f5332d74043a60f0a8c8721f320"
FBINK_SHA256 = "2ddc5fbbb6c0e98433bba8b356e492d383c0f10b6b6a856e89ef76a75ce9c5fe"
FILES = """
batch.sh busybox capture-frame dns-forward framework-overlay.apk fs-probe guest.sh
guard host.sh installer-probe kd-killd kobodroid-panel launch.sh launcher.apk
libGLES_kindleroid.so libkdkill.so policy-check power.sh probe.jar root.tar.gz
run-limit runner services-offline.jar session-input.jar setup.sh system-probe
system-survey.sh timezone usb-handoff.sh zram.sh
""".split()


def build(runtime, system, mke2fs, output, android_notice):
    if digest(system) != SYSTEM_SHA256:
        raise ValueError("System image does not match the verified V73 input")
    if "KOBODROID_V73" not in (runtime / "launch.sh").read_text():
        raise ValueError("Expected V73 runtime")
    files = {name: runtime / name for name in FILES}
    files["system.img"] = system
    archive_release(files, mke2fs, output, "V73",
                    {"notices/Android-SDK-NOTICE.txt": android_notice})


def archive_release(files, mke2fs, output, version, additional_files):
    with tempfile.TemporaryDirectory(prefix="kobodroid-release-") as staging:
        stage = Path(staging)
        seed = stage / "empty.img"
        config = stage / "mke2fs.conf"
        config.write_text("[defaults]\nbase_features = extent,filetype,sparse_super,large_file\n"
                          "[fs_types]\next4 = {\n features = has_journal\n}\n")
        with seed.open("wb") as file:
            file.truncate(DATA_BYTES)
        subprocess.run([mke2fs, "-q", "-F", "-t", "ext4", "-b", "4096",
                        "-E", "lazy_itable_init=0,lazy_journal_init=0", str(seed)],
                       env={**os.environ, "MKE2FS_CONFIG": str(config)}, check=True)
        with seed.open("rb") as source, gzip.open(stage / "data.img.gz", "wb", compresslevel=1) as dest:
            shutil.copyfileobj(source, dest, 4 * 1024**2)
        manifest = {
            "model": "N428",
            "firmware": "4.45.23646",
            "runtime_version": version,
            "runtime": {name: digest(path) for name, path in files.items()},
            "data_bytes": DATA_BYTES,
            "seed_gzip": digest(stage / "data.img.gz"),
            "seed": digest(seed),
            "fbink": FBINK_SHA256,
        }
        (stage / "release.json").write_text(json.dumps(manifest, indent=2) + "\n")
        here = Path(__file__).resolve().parent
        docs = here.parent / "docs"
        with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=3) as archive:
            for name, path in sorted(files.items()):
                archive.write(path, f"payload/{name}")
            for name in ("data.img.gz", "release.json"):
                archive.write(stage / name, name)
            archive.write(here / "install.py", "install.py")
            archive.write(docs / "INSTALL.md", "INSTALL.md")
            archive.write(docs / "RELEASE-NOTES.md", "RELEASE-NOTES.md")
            for notice in ("LICENSE", "NOTICE"):
                archive.write(here.parent / notice, notice)
            for name, path in additional_files.items():
                archive.write(path, name)
            for name in ("DISTRIBUTION.md", "HARDWARE-QA.md"):
                archive.write(docs / name, name)
            for notice in sorted((here.parent / "third-party").rglob("*")):
                if notice.is_file():
                    archive.write(notice, Path("notices") / notice.relative_to(here.parent / "third-party"))
            for name in ("pinyin-LICENSE.txt", "pinyin-NOTICE.txt"):
                archive.write(here.parent / "kobo-prototype/launcher/assets" / name, f"notices/{name}")
        with zipfile.ZipFile(output) as archive:
            if bad := archive.testzip():
                raise ValueError(f"Corrupted archive member: {bad}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("runtime", "system", "output", "android-notice"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--mke2fs", default="mke2fs")
    args = parser.parse_args()
    build(args.runtime, args.system, args.mke2fs, args.output, args.android_notice)
