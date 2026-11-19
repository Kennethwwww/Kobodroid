#!/bin/sh
# Rebuild only the private services.jar overlay from the pinned original SDK image.
set -eu
cd "$(dirname "$0")"
C=weread-jammy-cross
python3 - <<'PY'
from pathlib import Path
import hashlib
expected={'libsmali-java_2.4.0-1_all.deb':'863afae3bb245461142da80ff4c82bde7c41bc9801012a39eebde4f237150142','guava.deb':'b505a0472e5f34110a29a4f75844545b14f48e73c8657d02f2ac67bd32e28ee0','jcommander.deb':'a7260fb077703d07de817935ed77a71debf8838df03d207699600991e1bf475f','antlr.deb':'7ec392b5a8bef5a08155fdcaa6c465b82f0fe1c264c2432de5fdacc4b09114f5'}
for n,h in expected.items():assert hashlib.sha256((Path('build')/n).read_bytes()).hexdigest()==h,n
PY
docker exec "$C" mkdir -p /tmp/kobo-batch-rebuild/deps /tmp/kobo-batch-rebuild/framework /tmp/kobo-batch-rebuild/scripts
for name in libsmali-java_2.4.0-1_all.deb guava.deb jcommander.deb antlr.deb; do
 docker cp "build/$name" "$C:/tmp/kobo-batch-rebuild/$name"
done
docker cp ../../android7-prototype/device/build/system-min.img "$C:/tmp/kobo-batch-rebuild/system.img"
for script in patch-content-service.py patch-system-server.py patch-job-scheduler.py patch-network-metrics.py; do
 docker cp "src/$script" "$C:/tmp/kobo-batch-rebuild/scripts/$script"
done
docker exec "$C" sh -c '
set -e
W=/tmp/kobo-batch-rebuild
for p in "$W"/*.deb; do dpkg-deb -x "$p" "$W/deps"; done
CP="$W/deps/usr/share/java/*"
debugfs -R "rdump /framework $W/framework" "$W/system.img" > "$W/extract.log" 2>&1
java -Xmx1g -cp "$CP" org.jf.baksmali.Main deodex -a 24 -b "$W/framework/framework/arm/boot.oat" -d "$W/framework/framework" -d "$W/framework/framework/arm" -o "$W/smali" "$W/framework/framework/oat/arm/services.odex" > "$W/deodex.log" 2>&1
python3 "$W/scripts/patch-content-service.py" "$W/smali/com/android/server/content/ContentService.smali"
python3 "$W/scripts/patch-system-server.py" "$W/smali/com/android/server/SystemServer.smali"
python3 "$W/scripts/patch-job-scheduler.py" "$W/smali/com/android/server/job/JobSchedulerService.smali"
python3 "$W/scripts/patch-network-metrics.py" "$W/smali/com/android/server/connectivity/MetricsLoggerService.smali"
java -Xmx1g -cp "$CP" org.jf.smali.Main assemble --api 24 -o "$W/classes.dex" "$W/smali" > "$W/assemble.log" 2>&1
python3 - <<'"'"'PY'"'"'
from pathlib import Path
import zipfile
w=Path("/tmp/kobo-batch-rebuild")
with zipfile.ZipFile(w/"framework/framework/services.jar") as src,zipfile.ZipFile(w/"services-offline.jar","w",compression=zipfile.ZIP_DEFLATED) as out:
 for n in src.namelist():
  if not n.endswith(".dex"):out.writestr(n,src.read(n))
 out.write(w/"classes.dex","classes.dex")
PY'
docker cp "$C:/tmp/kobo-batch-rebuild/services-offline.jar" build/services-offline-rebuilt.jar
