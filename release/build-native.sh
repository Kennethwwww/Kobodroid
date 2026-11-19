#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source="$repo/kobo-prototype"
output=${1:-"$repo/build/native"}
mkdir -p "$output"
compile() {
    zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 "$@"
}
compile -DKOBO_SESSION_MEMORY_RECLAIM "$source/batch/src/runner.c" -o "$output/runner"
compile -I "$repo/android7-prototype/common" "$source/session/kobodroid-panel.c" -o "$output/kobodroid-panel"
compile -I "$repo/android7-prototype/common" "$source/session/frame-stream.c" -o "$output/capture-frame"
compile "$source/session/kd-killd.c" -o "$output/kd-killd"
compile "$source/src/fs-probe.c" -o "$output/fs-probe"
compile "$source/session/system-probe.c" -o "$output/system-probe"
compile "$source/batch/src/installer-probe.c" -o "$output/installer-probe"
compile "$source/batch/src/run-limit.c" -o "$output/run-limit"
