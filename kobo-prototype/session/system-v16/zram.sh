# Sourced by launch.sh while it holds the deployment lock.
# A marker in /tmp limits restoration to the swap configured by Kobodroid.
zram_sys=/sys/block/zram0
zram_dev=/dev/zram0
zram_owned=/tmp/kobodroid-zram-owned
zram_bytes=134217728

zram_active() {
    awk -v dev="$zram_dev" '$1 == dev {found = 1} END {exit !found}' /proc/swaps
}

zram_restore() (
    [ -f "$zram_owned" ] || return 0
    exec 8<>"$BASE/session.lock"
    /usr/bin/flock -n 8 || { echo ZRAM_RESTORE=SESSION_ACTIVE; return 1; }
    size=$(cat "$zram_sys/disksize") || return 1
    if [ "$size" = "$zram_bytes" ]; then
        if zram_active; then
            awk -v dev="$zram_dev" '$1 == dev {print "ZRAM_SWAP_USED_KIB=" $4}' /proc/swaps
            [ ! -r "$zram_sys/mm_stat" ] || sed 's/^/ZRAM_MM_STAT /' "$zram_sys/mm_stat"
            "$BASE/busybox" swapoff "$zram_dev" || return 1
        fi
        echo 1 > "$zram_sys/reset" || return 1
    elif [ "$size" != 0 ]; then
        echo "ZRAM_RESTORE=UNEXPECTED_SIZE bytes=$size"
        return 1
    fi
    rm -f "$zram_owned" || return 1
    echo ZRAM_RESTORE=PASS
)

zram_start() {
    [ -b "$zram_dev" ] && [ -w "$zram_sys/disksize" ] &&
        [ -w "$zram_sys/reset" ] && [ "$(cat "$zram_sys/disksize")" = 0 ] &&
        awk 'END {exit NR != 1}' /proc/swaps || return 1
    : > "$zram_owned" || return 1
    echo "$zram_bytes" > "$zram_sys/disksize" &&
        "$BASE/busybox" mkswap "$zram_dev" &&
        "$BASE/busybox" swapon "$zram_dev" || return 1
    echo "ZRAM_ENABLED bytes=$zram_bytes"
}
