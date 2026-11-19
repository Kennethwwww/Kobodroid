"""Apply Kobodroid's property storage compatibility to AOSP init source."""
import argparse
from pathlib import Path

BASE = """void property_init() {
    if (__system_property_area_init()) {"""
KOBO = """void property_init() {
    int result = __system_property_area_init();
    // The Kobo launcher sets this only after probing its private property mount.
    const char* compat = getenv("KOBODROID_PROPERTY_LABEL_COMPAT");
    bool allow_unlabeled = result == -2 && compat && strcmp(compat, "1") == 0;
    if (result != 0 && !allow_unlabeled) {"""


def patch(root):
    source = root / "system/core/init/property_service.cpp"
    code = source.read_text()
    if code.count(KOBO) == 1:
        return
    if code.count(BASE) != 1:
        raise ValueError(f"Unexpected property_init in {source}")
    source.write_text(code.replace(BASE, KOBO))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    patch(parser.parse_args().root)
