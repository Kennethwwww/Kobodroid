#!/usr/bin/env bash
set -euo pipefail
cd /chromium
revision=f6fcaed63cb61e2eeff44a3268ac6fbaaa2a8f88
test "$(git -C src rev-parse HEAD)" = "$revision"
origin=https://chromium.googlesource.com/chromium/src.git
if ! git -C src remote get-url origin >/dev/null 2>&1; then
    git -C src remote add origin "$origin"
fi
test "$(git -C src remote get-url origin)" = "$origin"
git config --global http.version HTTP/1.1
# This is the depot_tools commit in the pinned Chromium DEPS file.
tools_revision=90a30a5b5357636fa05bb315c393275be7ca705c
if [ ! -d depot_tools/.git ]; then
    git init depot_tools
fi
if ! git -C depot_tools cat-file -e "$tools_revision^{commit}"; then
    git -C depot_tools fetch --depth=1 \
        https://chromium.googlesource.com/chromium/tools/depot_tools.git "$tools_revision"
fi
git -C depot_tools checkout --detach "$tools_revision"
printf '%s\n' "$tools_revision" > depot-tools-revision.txt
export PATH=/chromium/depot_tools:$PATH DEPOT_TOOLS_UPDATE=0
if [ "${1:-}" = --hooks-only ]; then
    test -s .gclient
    grep -qF "$revision" resolved-chromium-deps.txt
    exec gclient runhooks
fi
cat > .gclient <<'GCLIENT'
solutions = [{
    "name": "src",
    "url": "https://chromium.googlesource.com/chromium/src.git",
    "managed": False,
    "custom_deps": {},
}]
target_os = ["android"]
GCLIENT
gclient sync --no-history --nohooks --jobs 5 --revision "src@$revision"
gclient revinfo -a > resolved-chromium-deps.txt
gclient runhooks
