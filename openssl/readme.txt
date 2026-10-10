This directory contains OpenSSL 3.5.8 LTS public headers and platform
configuration dispatchers. Upstream: https://www.openssl-library.org/source/
See ThirdPartyLicenses.txt for the Apache 2.0 license.

MinGW: run sh scripts/build-openssl.sh x64 (or x86 with a 32-bit toolchain)
in a MinGW/MSYS2 shell. Required tools: matching GCC/binutils, a complete Perl
installation, GNU make, curl, tar, sha256sum. MAKE and JOBS can be overridden.
This downloads a pinned, checksum-verified source archive, builds static
libraries, and copies their matching headers and platform configuration.

The x64 configuration uses no-shared no-module no-tests no-docs. A separate
configuration-mingw-x64.h prevents changing other toolchains' configuration.
Local MinGW x64 archives have been rebuilt and tested for 3.5.8. They live in
openssl/lib/x64; x86 archives go in openssl/lib. These archives are ignored
build artifacts, so fresh checkouts must run the script before building DC++.

MSVC: follow the native OpenSSL Configure/nmake instructions in Compile.txt
using the 3.5.8 source archive and the desired VC-WIN32 or VC-WIN64A target.
Install the generated headers/configuration for that target together with its
static archives. Keep opensslconf.h's existing per-toolchain dispatcher.
The x86/MSVC configurations and binaries were not rebuilt in this audit; do
not distribute those builds using stale binary archives with updated headers.

The header version is in include/openssl/opensslv.h. For the audit, archive
checksums, platform scope and validation see docs/dependency-updates.md.
