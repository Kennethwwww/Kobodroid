# Native toolchain

Kobodroid's ARM host tools use Zig 0.16.0 and its bundled musl. The FS, system and installer probes and the command timeout helper are built from the project source in `release/build-native.sh`. The notices here cover Zig and musl. [Zig 0.16.0](https://github.com/ziglang/zig/tree/0.16.0) · [Bundled musl source](https://github.com/ziglang/zig/tree/0.16.0/lib/libc/musl)

`dns-forward` is built with Go 1.26.4 without cgo. The program source is in `kobo-prototype/session/dns-forward.go` and the Go library notices are included here. [Go 1.26.4 source](https://go.googlesource.com/go/+/refs/tags/go1.26.4/)
