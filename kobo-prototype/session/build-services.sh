#!/bin/sh
set -eu
cd "$(dirname "$0")"
cross=weread-jammy-cross
docker exec "$cross" sh -c 'mkdir -p /tmp/kobo-session-services'
docker cp patch-lmkd.py "$cross:/tmp/kobo-session-services/patch-lmkd.py"
docker cp patch-connectivity.py "$cross:/tmp/kobo-session-services/patch-connectivity.py"
docker cp ../../android7-prototype/device/build/system-min.img "$cross:/tmp/kobo-session-services/system.img"
docker cp ../batch/package/services-offline.jar "$cross:/tmp/kobo-session-services/original.jar"
docker exec "$cross" python3 -c 'import zipfile; from pathlib import Path; w=Path("/tmp/kobo-session-services"); (w/"original.dex").write_bytes(zipfile.ZipFile(w/"original.jar").read("classes.dex"))'
docker exec "$cross" sh -c '
set -eu
w=/tmp/kobo-session-services
smali=$(mktemp -d "$w/smali.XXXXXX")
java -Xmx1g -cp "/usr/share/java/*:/tmp/kobo-smali-deps/usr/share/java/*" org.jf.baksmali.Main disassemble --api 24 -o "$smali" "$w/original.dex"
python3 "$w/patch-lmkd.py" "$smali/com/android/server/am/ProcessList.smali"
framework="$w/boot-framework.oat"
debugfs -R "dump /framework/arm/boot-framework.oat $framework" "$w/system.img"
java -Xmx1g -cp "/usr/share/java/*:/tmp/kobo-smali-deps/usr/share/java/*" org.jf.baksmali.Main disassemble --classes "Landroid/net/IConnectivityManager;" -o "$w/interface" "$framework"
python3 "$w/patch-connectivity.py" "$smali" "$w/interface/android/net/IConnectivityManager.smali"

java -Xmx1g -cp "/usr/share/java/*:/tmp/kobo-smali-deps/usr/share/java/*" org.jf.smali.Main assemble --api 24 -o "$w/classes.dex" "$smali"
python3 - <<'"'"'PY'"'"'
from pathlib import Path
import zipfile
w=Path("/tmp/kobo-session-services")
with zipfile.ZipFile(w/"original.jar") as src, zipfile.ZipFile(w/"services.jar","w",zipfile.ZIP_DEFLATED) as dst:
    for name in src.namelist():
        if name != "classes.dex":
            dst.writestr(name,src.read(name))
    dst.write(w/"classes.dex","classes.dex")
PY'
docker cp "$cross:/tmp/kobo-session-services/services.jar" services-home.jar
