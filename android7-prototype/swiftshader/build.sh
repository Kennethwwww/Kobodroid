#!/bin/sh
# Build the three ARMv7 SwiftShader GLES libraries for the AOSP API 24 image.
# The cross-build container supplies clang, the ARM GNU cross toolchain, CMake and Ninja.
# Output: ../build/swiftshader/. From compiler-rt builtins: emutls.c (thread_local on API 24), clear_cache.c + int_util.c. Only the GL part is built (Subzero JIT backend; no Vulkan, no LLVM 7, no samples).
# The source tree is never edited: CMakeLists.txt is patched in the build copy by patch-cmake.py.
set -eu
cd "$(dirname "$0")"
C=${KOBODROID_CROSS_CONTAINER:-weread-jammy-cross}
A=${KOBODROID_AOSP_HEADERS:-../aosp-r24/src}
ANDROID_LIBS=${KOBODROID_ANDROID_LIBS:-/tmp/a7/sys/lib}
OUTPUT=${KOBODROID_SWIFTSHADER_OUT:-../build/swiftshader}
W=$(docker exec "$C" mktemp -d /tmp/kd-a7-swiftshader.XXXXXXXX)
docker exec $C mkdir -p $W/src/third_party $W/hdr
# Only what the GL build reads (the full tree is 624 MB, mostly LLVM 7 and the PowerVR SDK).
for d in CMakeLists.txt include src third_party/subzero third_party/llvm-subzero third_party/stlport-cpp11-extension; do
  docker cp "src/$d" "$C:$W/src/$d" >/dev/null
done
docker cp builtins $C:$W/builtins
docker cp patch-cmake.py $C:$W/patch-cmake.py
docker cp shim $C:$W/shim
docker cp "$A/bionic" "$C:$W/hdr/bionic"
docker exec "$C" mkdir "$W/hdr/external"
docker cp "$A/external/libcxx" "$C:$W/hdr/external/libcxx"
docker cp "$A/external/libcxxabi" "$C:$W/hdr/external/libcxxabi"
docker cp "$A/system/core/include" "$C:$W/hdr/core-include"
docker cp "$A/hardware/libhardware/include" "$C:$W/hdr/hardware-include"
docker cp ../crt/dso_handle.c $C:$W/dso_handle.c
docker exec -e "ANDROID_LIBS=$ANDROID_LIBS" -w "$W" "$C" sh -eu -c '
python3 patch-cmake.py src/CMakeLists.txt
mkdir -p sysroot && cd "$ANDROID_LIBS" && cp libc.so libm.so libdl.so libc++.so liblog.so libcutils.so libutils.so libhardware.so '$W'/sysroot/ && cd '$W'
H='$W'/hdr; RES=$(clang -print-resource-dir)
TARGET="--target=armv7a-linux-androideabi24 -marm -march=armv7-a -mfpu=neon -mfloat-abi=softfp"
INC="-nostdinc -isystem $H/bionic/libc/arch-arm/include -isystem $H/bionic/libc/include -isystem $H/bionic/libc/kernel/uapi/asm-arm -isystem $H/bionic/libc/kernel/uapi -isystem $RES/include -I'$W'/shim -I$H/core-include -I$H/hardware-include -I'$W'/src/include/Android"
DEFS="-D__ANDROID__=1 -DANDROID=1 -DANDROID_PLATFORM_SDK_VERSION=24 -DDEFAULT_THREAD_COUNT=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
COMMON="$TARGET $INC $DEFS -include '$W'/shim/kd-compat.h -fPIC -fno-stack-protector -Wno-error"
CXX="-nostdinc++ -isystem $H/external/libcxx/include -isystem $H/external/libcxxabi/include $COMMON"
clang $COMMON -c dso_handle.c -o dso_handle.o
clang $COMMON -Ibuiltins -c builtins/emutls.c -o emutls.o
clang $COMMON -Ibuiltins -c builtins/clear_cache.c -o clear_cache.o  # JIT code: cacheflush syscall
clang $COMMON -Ibuiltins -c builtins/int_util.c -o int_util.o  # compilerrt_abort_impl for clear_cache
LD="--target=armv7a-linux-androideabi24 -fuse-ld=/usr/bin/arm-linux-gnueabihf-ld -nostdlib -Wl,--hash-style=both -Wl,--no-undefined -Wl,-z,noexecstack -Wl,--build-id=sha1 -L'$W'/sysroot '$W'/dso_handle.o '$W'/emutls.o '$W'/clear_cache.o '$W'/int_util.o $(arm-linux-gnueabihf-gcc -print-libgcc-file-name)"
cat > toolchain.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
EOF
cmake -S src -B out -G Ninja -DCMAKE_TOOLCHAIN_FILE='$W'/toolchain.cmake -DREACTOR_BACKEND=Subzero -DBUILD_VULKAN=OFF -DBUILD_SAMPLES=OFF -DBUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_FLAGS=$COMMON" "-DCMAKE_CXX_FLAGS=$CXX" "-DCMAKE_SHARED_LINKER_FLAGS=$LD" >/dev/null
cmake --build out --target libEGL libGLESv2 libGLES_CM -j4
'
mkdir -p "$OUTPUT"
for l in libEGL_swiftshader libGLESv2_swiftshader libGLESv1_CM_swiftshader; do
  docker exec "$C" sh -ec "f=\$(find $W/out -name '$l.so' | head -1); test -n \"\$f\"; arm-linux-gnueabihf-strip -o $W/$l.so \"\$f\""
  docker cp "$C:$W/$l.so" "$OUTPUT/$l.so" >/dev/null
done
ls -l "$OUTPUT"
