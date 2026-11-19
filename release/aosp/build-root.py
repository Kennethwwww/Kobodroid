"""Assemble a Kobo guest ramdisk from one AOSP build and source-built inputs."""
import argparse
from pathlib import Path
import re
import shutil
import tarfile
import tempfile

DAEMONS = {
    "audioserver", "debuggerd", "gatekeeperd", "installd", "keystore",
    "logd", "media", "mediacodec", "mediaextractor", "servicemanager",
    "surfaceflinger", "vold",
}
START_AT_BOOT = {"logd", "servicemanager", "vold"}
SWIFTSHADER = ("libEGL_swiftshader.so", "libGLESv1_CM_swiftshader.so",
               "libGLESv2_swiftshader.so")


def neutralize(text, *, root=False):
    """Keep only services Kobodroid starts in its private namespace."""
    output = []
    service = None
    in_skipped_service = False
    logical_lines = re.sub(r"\\\n[ \t]*", " ", text)
    for line in logical_lines.splitlines():
        words = line.strip().split()
        if not words:
            continue
        verb = words[0]
        if not line[0].isspace() and verb in {"on", "import", "service"}:
            service = None
            in_skipped_service = False
        if verb == "service":
            name = service = words[1]
            in_skipped_service = root or name not in DAEMONS
            if not in_skipped_service:
                output.extend((line, "    seclabel kernel"))
                if name == "servicemanager":
                    output.append("    user root")
                if name not in START_AT_BOOT:
                    output.append("    disabled")
            continue
        if in_skipped_service:
            continue
        if verb in {"import", "critical", "onrestart", "writepid", "seclabel",
                    "mount", "mount_all", "swapon", "ifup", "hostname",
                    "domainname", "init_user0", "reboot"}:
            continue
        if verb in {"write", "chmod", "chown"} and re.search(r"\s/(proc|sys)/", line):
            continue
        if verb == "setprop" and words[1] == "selinux.reload_policy":
            continue
        if verb == "user" and service == "servicemanager":
            continue
        output.append(line)
    return "\n".join(output) + "\n"


def put(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    shutil.copymode(source, target)


def build(product, extras, output):
    repo = Path(__file__).resolve().parents[2]
    templates = repo / "android7-prototype/rootfs-src"
    system = product / "system"
    init = product / "root/init"
    if b"KOBODROID_PROPERTY_LABEL_COMPAT" not in init.read_bytes():
        raise ValueError("Missing source-built Kobo-compatible init")
    with tempfile.TemporaryDirectory(prefix="kobodroid-root-") as temporary:
        root = Path(temporary) / "root"
        shutil.copytree(product / "root", root, symlinks=True)
        vendor = root / "vendor"
        if vendor.is_symlink():
            vendor.unlink()
        vendor.mkdir(exist_ok=True)
        for directory in ("system", "data", "dev", "proc", "sys"):
            (root / directory).mkdir(exist_ok=True)

        init_rc = root / "init.rc"
        imports = ("import /init.environ.rc\nimport /init.kindle.rc\n"
                   "import /init.zygote32.rc\n")
        init_rc.write_text(imports + neutralize(init_rc.read_text(), root=True))
        zygote = root / "init.zygote32.rc"
        text = zygote.read_text()
        anchor = "    class main\n"
        if text.count(anchor) != 1:
            raise ValueError("Unexpected AOSP zygote service")
        zygote.write_text("\n".join(line for line in text.splitlines()
                                    if not line.strip().startswith(("onrestart ", "writepid "))) + "\n")
        zygote.write_text(zygote.read_text().replace(
            anchor, anchor + "    disabled\n    seclabel kernel\n"))

        kindle = (templates / "init.kindle.rc").read_text()
        (root / "init.kindle.rc").write_text(kindle.replace(
            "/kdlib/libkdashmem.so:/kdlib/libkdnonwakealarm.so", "/kdlib/libkdnonwakealarm.so"))
        (root / "kdinit").mkdir()
        found = set()
        for rc in sorted((system / "etc/init").glob("*.rc")):
            text = rc.read_text()
            names = set(re.findall(r"^service\s+(\S+)", text, re.M))
            if names & DAEMONS:
                (root / "kdinit" / rc.name).write_text(neutralize(text))
                found |= names & DAEMONS
        required = {"logd", "servicemanager", "surfaceflinger", "installd", "vold"}
        if not required <= found:
            raise ValueError(f"Missing daemon definitions: {sorted(required - found)}")

        props = {}
        for content in ((root / "default.prop").read_text(),
                        (templates / "default.prop.append").read_text(),
                        (templates / "default.prop.slim").read_text()):
            for line in content.splitlines():
                if "=" in line and not line.lstrip().startswith("#"):
                    key, value = line.split("=", 1)
                    props[key] = value
        props.update({
            "ro.kindleroid.fb.width": "1264", "ro.kindleroid.fb.height": "1680",
            "ro.kindleroid.fb.dpi": "300", "ro.kindleroid.fb.fps": "5",
            "ro.sf.lcd_density": "300", "ro.kindleroid.host_memory_guard": "true",
            "config.disable_network": "true", "config.disable_systemui": "true",
            "config.disable_bluetooth": "true", "config.disable_location": "true",
            "config.disable_networktime": "true", "config.disable_samplingprof": "true",
            "config.disable_storage": "false", "ro.logd.kernel": "false",
            "ro.logd.auditd": "false", "persist.sys.usb.config": "none",
            "dalvik.vm.dex2oat-flags": "--runtime-arg -Xgc:SS",
        })
        (root / "default.prop").write_text("".join(f"{key}={value}\n" for key, value in props.items()))
        put(templates / "cmdline", root / "kdboot/cmdline")
        put(templates / "kd-data-layout.sh", root / "kdboot/kd-data-layout.sh")
        put(templates / "fstab.kindle", root / "fstab.kindle")
        hide = (templates / "hide-apps.txt").read_text().splitlines()
        (root / "kdboot/hide-apps").write_text("\n".join(
            line for line in hide if line and not line.startswith("#")) + "\n")
        put(templates / "handheld_core_hardware.xml", root / "kdoverlay/handheld_core_hardware.xml")

        for relative, target in (
            ("bin/kdhealthd", "kdbin/kdhealthd"),
            ("lib/libkdashmem.so", "kdlib/libkdashmem.so"),
            ("lib/libkdnonwakealarm.so", "kdlib/libkdnonwakealarm.so"),
            ("lib/hw/gralloc.kindle.so", "vendor/lib/hw/gralloc.kindle.so"),
        ):
            put(system / relative, root / target)
        put(extras / "probe.jar", root / "kdbin/kobo-probe.jar")
        for name in SWIFTSHADER:
            put(extras / name, root / "vendor/lib/kdgl" / name)
        (root / "vendor/app/KindleroidLauncher").mkdir(parents=True, exist_ok=True)
        (root / "vendor/lib/mediadrm").symlink_to("/system/vendor/lib/mediadrm")

        with tarfile.open(output, "x:gz") as archive:
            for path in sorted(root.iterdir()):
                archive.add(path, arcname=path.name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("product", type=Path, help="AOSP product output")
    parser.add_argument("extras", type=Path, help="Local source-built SwiftShader libraries and probe.jar")
    parser.add_argument("output", type=Path)
    options = parser.parse_args()
    build(options.product, options.extras, options.output)
