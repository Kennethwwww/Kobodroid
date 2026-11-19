#!/usr/bin/env python3
"""Boot an installation ZIP on the local ARM VM with a simulated Kobo panel."""
import argparse
import gzip
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

INIT = """#!/bin/busybox sh
BB=/bin/busybox
if [ ! -e /.switched ]; then
    $BB mkdir -p /mnt
    $BB mount -t tmpfs -o mode=0755 tmpfs /mnt || exit 2
    $BB cp -a /bin /pkg /init /usr /mnt/
    $BB mkdir -p /mnt/proc /mnt/sys /mnt/dev /mnt/tmp
    $BB touch /mnt/.switched
    exec $BB switch_root /mnt /init
fi
$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev
$BB --install -s /bin
export PATH=/bin
mkdir -p /mnt/onboard/.adds /mnt/onboard/.kobo
ln -s /pkg /mnt/onboard/.adds/kobodroid
printf 'N428,4.9.77,4.45.23646\\n' > /mnt/onboard/.kobo/version
for disk in /sys/block/vd?; do
    case $(cat "$disk/ro") in
        1) ln -s "/dev/${disk##*/}" /pkg/system.img ;;
        0) ln -s "/dev/${disk##*/}" /pkg/data.img ;;
        *) exit 3 ;;
    esac
done
[ -b /pkg/system.img ] && [ -b /pkg/data.img ] || exit 3
for session in $(seq 1 @SESSIONS@); do
    echo "LAB_SESSION=$session"
    /bin/with-host-fds /bin/sh /pkg/launch.sh
    for report in /pkg/reports/batch-*.txt; do
        [ -f "$report" ] && [ ! -e "$report.lab-read" ] || continue
        cat "$report"
        touch "$report.lab-read"
    done
done
poweroff -f
"""

HOST = """#!/bin/sh
cd "$2" || exit 1
for second in $(seq 1 540); do
    [ ! -f bridge/stop ] || exit 1
    if [ -f bridge/frame ]; then
        set -- $(od -An -tu4 -N12 bridge/frame)
        [ "$*" = "1264 1680 4" ] || exit 2
        [ "$(wc -c < bridge/frame)" = "$((12 + 1264 * 1680 * 2))" ] || exit 3
        echo LAB_HOME_FRAME=PASS
        mkfifo bridge/input || exit 4
        /bin/busybox timeout 12 sh -c 'printf "k 3\\n" > bridge/input' || exit 5
        sleep 2
        printf '0\\n' > bridge/result
        exit 0
    fi
    sleep 1
done
touch bridge/stop
exit 6
"""

MARKERS = (
    "ANDROID_HOME_STARTED=1",
    "LAUNCHER_BRIDGE=READY",
    "LAB_HOME_FRAME=PASS",
    "SESSION_INPUT key=3 accepted=true",
    "RUNNER_EXIT=0",
    "SESSION_CLEANUP=PASS remaining=0",
    "ENTRY_COMPLETE=1",
)

WEBVIEW_SMOKE = """    echo LAB_WEBVIEW_TEST=START
    run 120 /system/bin/sh /system/bin/am start -W -n org.kobodroid.lab/.WebViewSmoke || return 1
    webview_result=FAIL
    for attempt in $($BB seq 1 45); do
        if run 10 /system/bin/logcat -d -s KoboWebViewSmoke:I |
                $BB grep -q WEBVIEW_HTML_JS_PASS; then
            webview_result=PASS
            break
        fi
        $BB sleep 1
    done
    echo "LAB_WEBVIEW_TEST=$webview_result"
    [ "$webview_result" = PASS ] || return 1
    run 30 /system/bin/sh /system/bin/am force-stop org.kobodroid.lab || return 1
"""

BOOT_DIAGNOSTICS = """
echo LAB_BOOT_DIAGNOSTICS_BEGIN
run 25 /system/bin/logcat -d -b main -b system -b crash -v threadtime -t 300
echo LAB_BOOT_DIAGNOSTICS_END
"""

FLOCK = """
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "-n"))
        return 2;
    return flock(atoi(argv[2]), LOCK_EX | LOCK_NB) != 0;
}
"""

HOST_FDS = """
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    int wake = eventfd(0, EFD_CLOEXEC);
    int poll = epoll_create1(EPOLL_CLOEXEC);
    if (wake < 0 || poll < 0 || poll == 27) return 3;
    struct epoll_event event = {.events = EPOLLIN, .data.fd = wake};
    if (epoll_ctl(poll, EPOLL_CTL_ADD, wake, &event) || dup2(poll, 27) != 27) return 4;
    execv(argv[1], argv + 1);
    return 5;
}
"""

def write_initrd(root, output):
    with gzip.open(output, "wb") as stream:
        def add(name, mode, data, inode):
            name = name.encode() + b"\0"
            fields = (inode, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0)
            header = ("070701" + "".join(f"{field:08x}" for field in fields)).encode()
            stream.write(header + name + b"\0" * (-(len(header) + len(name)) % 4))
            stream.write(data + b"\0" * (-len(data) % 4))

        for index, entry in enumerate(sorted(root.rglob("*")), 1):
            add(str(entry.relative_to(root)), entry.stat().st_mode,
                entry.read_bytes() if entry.is_file() else b"", index)
        add("TRAILER!!!", 0, b"", 9999)


def prepare_webview_smoke(work, package):
    sdk = Path(os.environ.get("ANDROID_SDK", Path.home() / "Library/Android/sdk"))
    tools = sdk / "build-tools/35.0.1"
    android = sdk / "platforms/android-31/android.jar"
    source = Path(__file__).resolve().parent / "lab"
    build = work / "webview-smoke"
    build.mkdir()
    subprocess.run(["javac", "--release", "8", "-cp", android, "-d", build,
                    source / "WebViewSmoke.java"], check=True)
    subprocess.run([tools / "d8", "--min-api", "24", "--lib", android, "--output", build,
                    *sorted(build.rglob("*.class"))], check=True)
    subprocess.run([tools / "aapt", "package", "-M", source / "AndroidManifest.xml",
                    "-I", android, "-F", "unsigned.apk"], cwd=build, check=True)
    subprocess.run([tools / "aapt", "add", "unsigned.apk", "classes.dex"], cwd=build, check=True)
    subprocess.run([tools / "zipalign", "-f", "4", "unsigned.apk", "aligned.apk"],
                   cwd=build, check=True)
    subprocess.run(["keytool", "-genkeypair", "-keyalg", "RSA", "-alias", "lab",
                    "-dname", "CN=Kobodroid Lab", "-keystore", "lab.jks", "-storepass",
                    "lab-test-only", "-keypass", "lab-test-only", "-validity", "2"],
                   cwd=build, check=True)
    subprocess.run([tools / "apksigner", "sign", "--ks", "lab.jks", "--ks-pass",
                    "pass:lab-test-only", "--out", package / "install.apk", "aligned.apk"],
                   cwd=build, check=True)
    guest = package / "guest.sh"
    text = guest.read_text()
    anchor = "    phase home\n"
    if text.count(anchor) != 1:
        raise ValueError("Missing guest smoke-test anchor")
    guest.write_text(text.replace(anchor, WEBVIEW_SMOKE + anchor))


def boot(options):
    source = Path(__file__).resolve().parents[2] / "kobo-prototype/batch/src/runner.c"
    with tempfile.TemporaryDirectory(prefix="kobodroid-lab-") as temp:
        work = Path(temp)
        root = work / "initrd"
        package = root / "pkg"
        package.mkdir(parents=True)
        (root / "bin").mkdir()
        (root / "usr/bin").mkdir(parents=True)
        with zipfile.ZipFile(options.archive) as release:
            for name in release.namelist():
                if name.startswith("payload/"):
                    filename = Path(name).name
                    if filename == "system.img" and options.runtime_overlay:
                        continue
                    target = work / filename if filename == "system.img" else package / filename
                    with release.open(name) as src, target.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
                    target.chmod(0o755)
            if options.data_image:
                shutil.copyfile(options.data_image, work / "data.img")
            else:
                with release.open("data.img.gz") as src, gzip.GzipFile(fileobj=src) as seed, \
                        (work / "data.img").open("wb") as dst:
                    shutil.copyfileobj(seed, dst)
        shutil.copy2(package / "busybox", root / "bin/busybox")
        system_image = work / "system.img"
        if options.runtime_overlay:
            system_image = options.runtime_overlay / "system.img"
            for name in ("root.tar.gz", "setup.sh", "libkdkill.so"):
                shutil.copy2(options.runtime_overlay / name, package / name)
            if (options.runtime_overlay / "guest.sh").is_file():
                shutil.copy2(options.runtime_overlay / "guest.sh", package / "guest.sh")
            for name in ("services-offline.jar", "libGLES_kindleroid.so"):
                (package / name).unlink(missing_ok=True)
        if options.native_tools:
            for name in ("fs-probe", "installer-probe", "run-limit", "system-probe"):
                shutil.copy2(options.native_tools / name, package / name)
        if options.boot_diagnostics:
            batch = package / "batch.sh"
            text = batch.read_text()
            poll = 'for i in $($BB seq 1 480); do'
            ready = 'if [ "$ready" = 1 ]; then'
            if text.count(poll) != 1 or text.count(ready) != 1:
                raise ValueError("Missing diagnostic anchors in batch.sh")
            text = text.replace(poll, 'for i in $($BB seq 1 20); do')
            batch.write_text(text.replace(ready, BOOT_DIAGNOSTICS + ready))
        (root / "init").write_text(INIT.replace("@SESSIONS@", str(options.sessions)))
        (root / "init").chmod(0o755)
        (package / "host.sh").write_text(HOST)
        if options.webview:
            prepare_webview_smoke(work, package)
        subprocess.run(["zig", "cc", "-target", "arm-linux-musleabihf", "-mcpu=cortex_a7",
                        "-static", "-O2", "-DKOBO_LAB", "-DKOBO_SESSION_MEMORY_RECLAIM",
                        str(source), "-o", str(package / "runner")], check=True)
        (work / "flock.c").write_text(FLOCK)
        subprocess.run(["zig", "cc", "-target", "arm-linux-musleabihf", "-mcpu=cortex_a7",
                        "-static", "-O2", str(work / "flock.c"),
                        "-o", str(root / "usr/bin/flock")], check=True)
        (work / "with-host-fds.c").write_text(HOST_FDS)
        subprocess.run(["zig", "cc", "-target", "arm-linux-musleabihf", "-mcpu=cortex_a7",
                        "-static", "-O2", str(work / "with-host-fds.c"),
                        "-o", str(root / "bin/with-host-fds")], check=True)
        # In the lab the system disk is separate and the Kobo panel is simulated.
        entries = sorted(package.iterdir())
        with (package / "checksums.sha256").open("w") as output:
            for entry in entries:
                digest = subprocess.check_output(["shasum", "-a", "256", str(entry)], text=True)
                output.write(f"{digest.split()[0]}  {entry.name}\n")
        image = work / "initrd.gz"
        write_initrd(root, image)
        subprocess.run([
            "qemu-system-arm", "-M", "virt,highmem=off", "-cpu", "cortex-a7",
            "-smp", "2", "-m", str(options.memory_mib), "-accel", "tcg,thread=multi",
            "-kernel", str(options.kernel), "-initrd", str(image),
            "-append", "console=ttyAMA0 printk.devkmsg=on rdinit=/init",
            "-drive", f"if=none,file={system_image},format=raw,id=sys,readonly=on",
            "-device", "virtio-blk-device,drive=sys",
            "-drive", f"if=none,file={work / 'data.img'},format=raw,id=data",
            "-device", "virtio-blk-device,drive=data",
            "-nographic", "-no-reboot", "-monitor", "none",
            "-serial", f"file:{options.log}",
        ], timeout=660 * options.sessions, check=True)
    log = options.log.read_text(errors="replace")
    expected = (*MARKERS, "LAB_WEBVIEW_TEST=PASS") if options.webview else MARKERS
    for session in range(1, options.sessions + 1):
        start = f"LAB_SESSION={session}\n"
        _, present, remaining = log.partition(start)
        transcript = remaining.partition(f"LAB_SESSION={session + 1}\n")[0]
        if session == 1 and options.data_image:
            defaults = ("ANDROID_DEFAULTS_INITIALIZED=1", "ANDROID_DEFAULTS_REUSED=1")
        else:
            defaults = ("ANDROID_DEFAULTS_INITIALIZED=1",) if session == 1 else ("ANDROID_DEFAULTS_REUSED=1",)
        missing = [marker for marker in expected if marker not in transcript]
        if not any(marker in transcript for marker in defaults):
            missing.append(" or ".join(defaults))
        if not re.search(r"^HOST_FDS_CLOSED=[1-9][0-9]*$", transcript, re.MULTILINE):
            missing.append("inherited host descriptors closed")
        if not present or missing:
            raise ValueError(f"VM session {session} markers missing: {missing}")
        crash = re.search(
            r"init: Service '(?:zygote|vold|servicemanager|installd|surfaceflinger)' .*"
            r"killed by signal (?:6|11)\b", transcript)
        if crash:
            raise ValueError(f"VM session {session} daemon crash: {crash.group()}")
    print("AOSP_QEMU_BOOT=PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("log", type=Path)
    parser.add_argument("--webview", action="store_true", help="also run offline WebView smoke APK")
    parser.add_argument("--native-tools", type=Path, help="test updated native tools over an existing ZIP")
    parser.add_argument("--runtime-overlay", type=Path, help="boot a staged runtime during integration")
    parser.add_argument("--data-image", type=Path, help="boot a disposable copy of an existing data image")
    parser.add_argument("--boot-diagnostics", action="store_true", help="capture early Android logs")
    parser.add_argument("--sessions", choices=(1, 2), type=int, default=1)
    parser.add_argument("--memory-mib", choices=(1024, 1536), type=int, default=1536)
    boot(parser.parse_args())
