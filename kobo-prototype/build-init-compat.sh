#!/bin/sh
set -eu
cd "$(dirname "$0")"
C=weread-jammy-cross
mkdir -p build
docker cp src/property-init-compat.S "$C:/tmp/kobo-property-compat.S"
docker cp src/property-init-targets.S "$C:/tmp/kobo-property-targets.S"
docker exec "$C" sh -c '
set -e
cat /tmp/kobo-property-compat.S /tmp/kobo-property-targets.S > /tmp/kobo-property-linked.S
arm-linux-gnueabihf-as -march=armv7-a -mthumb /tmp/kobo-property-linked.S -o /tmp/kobo-property-linked.o
arm-linux-gnueabihf-ld -Ttext=0x11748 -e property_init_compat /tmp/kobo-property-linked.o -o /tmp/kobo-property-compat.elf
arm-linux-gnueabihf-objcopy -O binary -j .text /tmp/kobo-property-compat.elf /tmp/kobo-property-compat.bin'
docker cp "$C:/tmp/kobo-property-compat.bin" build/property-init-compat.bin
python3 src/patch-init.py
