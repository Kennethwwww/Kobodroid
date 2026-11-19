# BusyBox corresponding source

The runtime ships the unmodified `./usr/bin/busybox` from the Ubuntu Noble ARM hard-float package `busybox-static_1.36.1-6ubuntu3_armhf.deb`. The extracted executable was compared byte for byte with the Kobodroid binary. Its SHA-256 is `65f930d853baa8399e41c305745dc81b171cd62e2d74813f977598cd9f739fcf`.

Publish the companion `Kobodroid-v0.1.0-preview.1-busybox-source.zip` alongside the installation package. It contains the BusyBox source, Ubuntu patches, build configuration and rules, plus the corresponding glibc and GCC source packages. Ubuntu's ARM build record identifies glibc `2.39-0ubuntu8` and GCC `13.2.0-23ubuntu4`. Source member checksums match their Ubuntu descriptors. [Ubuntu ARM build record](https://launchpad.net/ubuntu/+source/busybox/1:1.36.1-6ubuntu3/+build/28125292) · [BusyBox license](https://busybox.net/license.html)
