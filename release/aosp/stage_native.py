"""Register native Kobo source modules in a local Nougat AOSP checkout."""
import argparse
from pathlib import Path
import shutil


PRODUCT_MODULES = """# Kobodroid native runtime
PRODUCT_PACKAGES += gralloc.kindle libGLES_kindleroid kdhealthd
PRODUCT_PACKAGES += libkdashmem libkdnonwakealarm libkdkill
"""


def stage(aosp):
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    if not (aosp / "build/envsetup.sh").is_file():
        raise ValueError("Expected an initialized AOSP source tree")
    out = aosp / "device/kobodroid/native"
    out.mkdir(parents=True, exist_ok=True)
    for source, name in (
        (repo / "android7-prototype/hal-src", "hal"),
        (repo / "android7-prototype/common", "common"),
        (repo / "android7-prototype/shim-src", "shims"),
        (repo / "kobo-prototype/gles", "gles"),
    ):
        for item in sorted(source.rglob("*")):
            if item.is_file():
                target = out / name / item.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, target)
    shutil.copy2(here / "Android.mk", out / "Android.mk")
    product = aosp / "build/target/product/aosp_arm.mk"
    config = product.read_text()
    if PRODUCT_MODULES not in config:
        product.write_text(config.rstrip() + "\n\n" + PRODUCT_MODULES)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("aosp", type=Path)
    stage(parser.parse_args().aosp)
