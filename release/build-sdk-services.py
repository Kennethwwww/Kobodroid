#!/usr/bin/env python3
"""Build the V73 Android service overlay from a local SDK system image."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile

from install import digest
from extract_sdk_input import SYSTEM_SHA256 as SDK_SYSTEM_SHA256

V73_SYSTEM_SHA256 = "87c7ac4b466355da4ba11cd94b70f3b9ffd40f5332d74043a60f0a8c8721f320"


def build(image, output, classpath, debugfs):
    if digest(image) not in {SDK_SYSTEM_SHA256, V73_SYSTEM_SHA256}:
        raise ValueError("Expected the pinned ARM API 24 r07 system image")
    project = Path(__file__).resolve().parent.parent / "kobo-prototype"
    with tempfile.TemporaryDirectory(prefix="kobodroid-services-") as temp:
        work = Path(temp)

        def smali(tool, *args):
            subprocess.run(["java", "-Xmx1g", "-cp", classpath,
                            f"org.jf.{tool}.Main", *args], check=True)

        def patch(script, *args):
            subprocess.run(["python3", str(project / script), *args], check=True)

        subprocess.run([debugfs, "-R", "rdump /framework .", str(image.resolve())], cwd=work, check=True)
        framework = work / "framework"
        code = work / "smali"
        boot = framework / "arm"
        smali("baksmali", "deodex", "-a", "24", "-b", str(boot / "boot.oat"),
              "-d", str(framework), "-d", str(boot), "-o", str(code),
              str(framework / "oat/arm/services.odex"))
        server = code / "com/android/server"
        for script, source in (
            ("patch-content-service.py", "content/ContentService.smali"),
            ("patch-system-server.py", "SystemServer.smali"),
            ("patch-job-scheduler.py", "job/JobSchedulerService.smali"),
            ("patch-network-metrics.py", "connectivity/MetricsLoggerService.smali"),
        ):
            patch(f"batch/src/{script}", str(server / source))

        iface = work / "interface"
        smali("baksmali", "disassemble", "--classes", "Landroid/net/IConnectivityManager;,Landroid/net/wifi/IWifiManager;",
              "-o", str(iface), str(boot / "boot-framework.oat"))
        patch("session/patch-lmkd.py", str(server / "am/ProcessList.smali"))
        patch("session/patch-connectivity.py", str(code),
              str(iface / "android/net/IConnectivityManager.smali"))
        patch("session/patch-wifi.py", str(code), str(iface / "android/net/wifi/IWifiManager.smali"))
        dex = work / "classes.dex"
        smali("smali", "assemble", "--api", "24", "-o", str(dex), str(code))
        with zipfile.ZipFile(framework / "services.jar") as source, \
                zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as dest:
            for name in source.namelist():
                if not name.endswith(".dex"):
                    dest.writestr(name, source.read(name))
            dest.write(dex, "classes.dex")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--classpath", required=True, help="Java classpath for smali and its dependencies")
    parser.add_argument("--debugfs", default="debugfs")
    options = parser.parse_args()
    build(options.image, options.output, options.classpath, options.debugfs)
