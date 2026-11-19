#!/bin/sh
# Before Android init: validate the SDK's primary-user alias without deleting app data.
set -eu
BB=${KD_BB:?}
D=${1:?}
for part in "$D" "$D/data" "$D/user"; do
    if [ -L "$part" ]; then echo "DATA_LAYOUT_REFUSED symlink-parent=$part"; exit 1; fi
    if [ -e "$part" ] && [ ! -d "$part" ]; then echo "DATA_LAYOUT_REFUSED non-directory=$part"; exit 1; fi
done
$BB mkdir -p "$D/data" "$D/user"
U="$D/user/0"
if [ -L "$U" ]; then
    target=$($BB readlink "$U")
    case "$target" in
        /data/data|../data) ;;
        *) echo "DATA_LAYOUT_REFUSED unexpected-user0-link"; exit 1;;
    esac
    # Relative form resolves correctly both before and after chroot.
    if [ "$target" = /data/data ]; then
        $BB ln -sfn ../data "$U"
        echo 'DATA_LAYOUT_REPAIRED absolute-primary-alias'
    fi
elif [ -d "$U" ]; then
    canonical=$($BB stat -Lc '%d:%i' "$D/data")
    actual=$($BB stat -Lc '%d:%i' "$U")
    if [ "$canonical" != "$actual" ]; then
        # rmdir succeeds only for an empty directory. Never hide or delete existing app files.
        if ! $BB rmdir "$U"; then echo 'DATA_LAYOUT_REFUSED divergent-nonempty-user0'; exit 1; fi
        $BB ln -s ../data "$U" || { $BB mkdir "$U"; exit 1; }
        echo 'DATA_LAYOUT_REPAIRED empty-primary-directory'
    fi
elif [ -e "$U" ]; then
    echo 'DATA_LAYOUT_REFUSED primary-path-not-directory'; exit 1
else
    $BB ln -s ../data "$U"
    echo 'DATA_LAYOUT_REPAIRED missing-primary-alias'
fi
[ "$($BB stat -Lc '%d:%i' "$D/data")" = "$($BB stat -Lc '%d:%i' "$U")" ] || { echo 'DATA_LAYOUT_REFUSED primary-alias-mismatch'; exit 1; }
echo 'DATA_LAYOUT_RESULT=PASS primary_alias=data/data user0_same_inode=1'
