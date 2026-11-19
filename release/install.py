#!/usr/bin/env python3
"""Install Kobodroid on the USB volume of a supported Kobo."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil

DATA_BYTES = 4086 * 1024**2
MENU = ("menu_item :main :Kobodroid :cmd_spawn :quiet :exec /bin/sh "
        "/mnt/onboard/.adds/kobodroid/launch.sh freeze\n"
        "    chain_success :dbg_toast :正在准备 Kobodroid…\n")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(4 * 1024**2), b""):
            h.update(chunk)
    return h.hexdigest()


def install_file(source, target, expected, compressed=False):
    temporary = target.with_name(target.name + ".installing")
    try:
        with (gzip.open(source, "rb") if compressed else source.open("rb")) as stream:
            with temporary.open("wb") as output:
                shutil.copyfileobj(stream, output, 4 * 1024**2)
        if digest(temporary) != expected:
            raise ValueError(f"Copy checksum mismatch: {target.name}")
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def publish_text(target, content):
    temporary = target.with_name(target.name + ".installing")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(target)


def install(volume, release):
    model, kernel, firmware, *_ = (volume / ".kobo/version").read_text().strip().split(",")
    if not model.startswith("N428") or (kernel, firmware) != ("4.9.77", "4.45.23646"):
        raise ValueError("Requires Kobo Libra Colour N428, firmware 4.45.23646")
    manifest = json.loads((release / "release.json").read_text())
    if (manifest["model"], manifest["firmware"], manifest["data_bytes"]) != ("N428", firmware, DATA_BYTES):
        raise ValueError("Release metadata does not match this installer")
    runtime = manifest["runtime"]
    if not {"launch.sh", "system.img", "runner", "kobodroid-panel"} <= runtime.keys():
        raise ValueError("Incomplete runtime manifest")
    for name, expected in runtime.items():
        if Path(name).name != name or name.startswith(".") or name in {"data.img", "settings"}:
            raise ValueError(f"Unexpected runtime filename: {name}")
        if digest(release / "payload" / name) != expected:
            raise ValueError(f"Release checksum mismatch: {name}")

    adds = volume / ".adds"
    menu = adds / "nm"
    if not (menu / "doc").is_file():
        raise ValueError("Install NickelMenu and restart the Kobo before continuing")
    if digest(adds / "koreader" / "fbink") != manifest["fbink"]:
        raise ValueError("Requires the tested KOReader v2025.10 FBInk build")
    target = adds / "kobodroid"
    if (target / ".deploy-pending").exists():
        raise ValueError("Incomplete SSH deployment. Restore that deployment first.")
    data = target / "data.img"
    if data.exists():
        with data.open("rb") as source:
            source.seek(1080)
            if source.read(2) != b"\x53\xef":
                raise ValueError("Existing data.img is not ext4. It was preserved.")
    elif digest(release / "data.img.gz") != manifest["seed_gzip"]:
        raise ValueError("Empty data image archive checksum mismatch")

    updates = [(name, expected) for name, expected in runtime.items()
               if not (target / name).is_file() or digest(target / name) != expected]
    needed = sum((release / "payload" / name).stat().st_size for name, _ in updates)
    if not data.exists():
        needed += DATA_BYTES
    if shutil.disk_usage(volume).free < needed + 32 * 1024**2:
        raise ValueError(f"Insufficient space. Need at least {needed // 1024**2 + 32} MiB free.")

    target.mkdir(exist_ok=True)
    if (target / "timezone").is_file():
        runtime["timezone"] = digest(target / "timezone")
        updates = [(name, expected) for name, expected in updates if name != "timezone"]
    for name, expected in updates:
        install_file(release / "payload" / name, target / name, expected)
    if not data.exists():
        install_file(release / "data.img.gz", data, manifest["seed"], compressed=True)
    checksums = {**runtime, "../koreader/fbink": manifest["fbink"]}
    publish_text(target / "checksums.sha256",
                 "".join(f"{value}  {name}\n" for name, value in sorted(checksums.items())))
    (volume / "Kobodroid-APK").mkdir(exist_ok=True)
    publish_text(menu / "kobodroid", MENU)
    print("Kobodroid installed. Safely eject the Kobo and open NickelMenu → Kobodroid.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("volume", type=Path, help="USB volume of the Kobo eReader")
    parser.add_argument("--release", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    try:
        install(args.volume, args.release)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")
