"""Use AOSP-built init, Binder, framework, and GLES in the Kobo guest."""
import argparse
from pathlib import Path


def adapt(source, output):
    setup = source.read_text()
    old_probe = """case "$label_status" in
 0) echo PROPERTY_INIT_MODE=original ;;
 95) $BB cp init-kobo-compat init; echo PROPERTY_INIT_MODE=kobo-label-compat ;;
 *) echo PROPERTY_INIT_MODE=refused; exit 11 ;;
esac"""
    probe = """property_label_compat=0
case "$label_status" in
 0) echo PROPERTY_INIT_MODE=aosp ;;
 95) property_label_compat=1; echo PROPERTY_INIT_MODE=aosp-kobo-label-compat ;;
 *) echo PROPERTY_INIT_MODE=refused; exit 11 ;;
esac"""
    old_launch = 'ANDROID_DATA=/data /init --second-stage &'
    launch = ('ANDROID_DATA=/data KOBODROID_PROPERTY_LABEL_COMPAT="$property_label_compat" '
              '/init --second-stage &')
    legacy_overrides = """$BB mount --bind kdoverlay/libbinder.so system/lib/libbinder.so
$BB mount -o remount,bind,ro system/lib/libbinder.so
$BB mount --bind kdoverlay/servicemanager system/bin/servicemanager
$BB mount -o remount,bind,ro system/bin/servicemanager
$BB mount --bind "$BASE/services-offline.jar" system/framework/services.jar
$BB mount -o remount,bind,ro system/framework/services.jar
# The overlaid jar must not use the original jar's preoptimized code.
$BB touch kd-empty-services-odex
$BB mount --bind kd-empty-services-odex system/framework/oat/arm/services.odex
$BB mount -o remount,bind,ro system/framework/oat/arm/services.odex
"""
    stock_dispatch = '$BB cp "$BASE/libGLES_kindleroid.so" vendor/lib/kdgl/libGLES_kindleroid.so'
    aosp_dispatch = (
        "$BB mkdir -p vendor/lib/kdgl\n"
        "$BB cp system/lib/egl/libGLES_android.so vendor/lib/kdgl/libGLES_android_agl.so\n"
        "$BB cp system/lib/libGLES_kindleroid.so vendor/lib/kdgl/libGLES_kindleroid.so"
    )
    for old, new in (
        (old_probe, probe),
        (old_launch, launch),
        (legacy_overrides, ""),
        (stock_dispatch, aosp_dispatch),
    ):
        if setup.count(old) != 1:
            raise ValueError(f"Unexpected setup.sh: {source}")
        setup = setup.replace(old, new)
    with output.open("x") as file:
        file.write(setup)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    adapt(args.source, args.output)
