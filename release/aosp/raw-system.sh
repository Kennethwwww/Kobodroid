#!/usr/bin/env bash
set -euo pipefail
input=${1:?system image}
output=${2:?raw system image output}
simg2img=${3:?AOSP simg2img}

if [ "$(od -An -tx1 -N4 "$input" | tr -d ' \n')" = 3aff26ed ]; then
    "$simg2img" "$input" "$output"
elif [ "$(od -An -tx1 -j1080 -N2 "$input" | tr -d ' \n')" = 53ef ]; then
    cp --sparse=always "$input" "$output"
else
    echo "Unknown AOSP system image format: $input" >&2
    exit 1
fi
