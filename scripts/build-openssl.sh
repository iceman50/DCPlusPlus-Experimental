#!/bin/sh
# Copyright (C) 2026 iceman50
# Run in a MinGW/MSYS2 shell with matching GCC, Perl, make, curl and tar on PATH.
set -eu

arch=${1:-x64}
case "$arch" in
    x64) platform=mingw64; libdir=openssl/lib/x64 ;;
    x86) platform=mingw; libdir=openssl/lib ;;
    *) echo "Usage: $0 [x64|x86]" >&2; exit 2 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=3.5.8
archive="openssl-$version.tar.gz"
digest=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
work="$root/build/openssl-$version-$arch"
mkdir -p "$work"
cd "$work"
if [ ! -f "$archive" ]; then
    curl --fail --location --output "$archive.download" \
        "https://github.com/openssl/openssl/releases/download/openssl-$version/$archive"
    mv "$archive.download" "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | sha256sum --check -
if [ ! -d "openssl-$version" ]; then
    tar -xzf "$archive"
fi
cd "openssl-$version"
perl Configure "$platform" no-shared no-module no-tests no-docs
"${MAKE:-make}" -j"${JOBS:-8}" build_libs

# Publish a matched set only after the build succeeds. Keep the platform dispatchers.
mkdir -p "$root/$libdir"
cp libssl.a libcrypto.a "$root/$libdir/"
for header in include/openssl/*.h; do
    case "${header##*/}" in opensslconf.h|configuration.h) continue ;; esac
    cp "$header" "$root/openssl/include/openssl/"
done
cp include/openssl/configuration.h \
    "$root/openssl/include/openssl/configuration-mingw-$arch.h"
sed "s@<openssl/configuration.h>@<openssl/configuration-mingw-$arch.h>@" \
    include/openssl/opensslconf.h > "$root/openssl/include/openssl/opensslconf-mingw-$arch.h"
printf 'Built OpenSSL %s for %s in %s\n' "$version" "$arch" "$root/$libdir"
