#!/usr/bin/env python3
"""Extract pinned inputs from a separately acquired Android SDK archive."""
import argparse
from pathlib import Path
import shutil
import tempfile
import zipfile

from install import digest


ARCHIVE_SHA256 = "42690977fe4b97b0643da11a2b42742b2e3d5b8849dcdd6893e0b92d6618c91d"
SYSTEM_SHA256 = "72fd6a04e76721dceecf3ab63ed529fae3fd658109dcd3b4633ed4e6f568fa27"
MEMBERS = ("system.img", "ramdisk.img", "NOTICE.txt", "source.properties")


def extract(archive_path, output):
    if digest(archive_path) != ARCHIVE_SHA256:
        raise ValueError("Unexpected Android SDK archive SHA-256")
    if output.exists():
        raise ValueError("Choose a new output directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".sdk-", dir=output.parent) as temporary:
        staged = Path(temporary) / "inputs"
        staged.mkdir()
        with zipfile.ZipFile(archive_path) as archive:
            for name in MEMBERS:
                with archive.open(f"armeabi-v7a/{name}") as source, (staged / name).open("wb") as dest:
                    shutil.copyfileobj(source, dest, 4 * 1024**2)
        if digest(staged / "system.img") != SYSTEM_SHA256:
            raise ValueError("Unexpected system.img SHA-256")
        staged.rename(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="Locally obtained armeabi-v7a-24_r07.zip")
    parser.add_argument("output", type=Path, help="New directory for local build inputs")
    args = parser.parse_args()
    try:
        extract(args.archive, args.output)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"{error}\n")
    print(f"SDK inputs verified and extracted to {args.output}")
