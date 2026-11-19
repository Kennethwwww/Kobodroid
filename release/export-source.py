#!/usr/bin/env python3
"""Export the Kobo source and rebuild instructions as a GitHub release asset."""
import argparse
from pathlib import Path
import tempfile
import zipfile


PROJECT = Path(__file__).resolve().parents[1]
ENTRIES = {
    ".gitignore", "LICENSE", "NOTICE", "README.md",
    "android7-prototype", "docs", "kobo-prototype", "release", "third-party",
}
GENERATED = {"__pycache__", ".DS_Store", ".pytest_cache"}
BINARIES = {".apk", ".class", ".dex", ".img", ".jar", ".pem", ".pk8", ".pyc", ".so"}


def build(output):
    with tempfile.TemporaryDirectory(prefix="kobodroid-source-", dir=output.parent) as folder:
        archive_path = Path(folder) / output.name
        with zipfile.ZipFile(archive_path, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for member in sorted(ENTRIES):
                entry = PROJECT / member
                files = entry.rglob("*") if entry.is_dir() else [entry]
                for path in sorted(files):
                    relative = path.relative_to(PROJECT)
                    if GENERATED.intersection(relative.parts) or not path.is_file() or path.is_symlink():
                        continue
                    if path.suffix in BINARIES:
                        raise ValueError(f"Unexpected generated asset: {relative}")
                    archive.write(path, Path("Kobodroid") / relative)
        with zipfile.ZipFile(archive_path) as archive:
            corrupt = archive.testzip()
            if corrupt is not None:
                raise ValueError(f"Corrupted source archive member: {corrupt}")
        archive_path.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    build(parser.parse_args().output)
