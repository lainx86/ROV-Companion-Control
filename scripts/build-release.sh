#!/bin/sh
# Run inside a Debian 12 container with the source at /src and output at /out.
set -eu

dpkg --add-architecture arm64
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential cmake python3 libncurses-dev libncurses-dev:arm64 \
  g++-aarch64-linux-gnu qemu-user ca-certificates

cmake -S /src -B /out/build-x86_64 -DCMAKE_BUILD_TYPE=Release
cmake --build /out/build-x86_64 --parallel 2
ctest --test-dir /out/build-x86_64 --output-on-failure

cmake -S /src -B /out/build-arm64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  '-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64;-L;/usr/aarch64-linux-gnu' \
  -DCURSES_LIBRARY=/usr/lib/aarch64-linux-gnu/libncursesw.so \
  -DCURSES_FORM_LIBRARY=/usr/lib/aarch64-linux-gnu/libformw.so \
  -DCURSES_INCLUDE_PATH=/usr/include
cmake --build /out/build-arm64 --parallel 2
ctest --test-dir /out/build-arm64 --output-on-failure

for arch in x86_64 arm64; do
  package="rov-control-v0.1.0-linux-$arch"
  mkdir -p "/out/$package"
  install -m 755 "/out/build-$arch/rov_control" "/out/$package/rov_control"
  if [ "$arch" = arm64 ]; then
    aarch64-linux-gnu-strip "/out/$package/rov_control"
  else
    strip "/out/$package/rov_control"
  fi
  install -m 644 /src/README.md "/out/$package/README.md"
  tar -C /out -czf "/out/$package.tar.gz" "$package"
done
