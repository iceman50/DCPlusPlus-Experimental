<!-- Copyright (C) 2026 iceman50 -->
# Bundled dependency audit — 2026-09-27

The SCons library targets and their upstream releases were reviewed. Application
code, build tools, and Windows SDK/system libraries are separate from these
vendored libraries.

| Library | Previous copy | Selected version | Upstream |
| --- | --- | --- | --- |
| zlib / bundled MiniZip | 1.3.1 | 1.3.2 | [zlib](https://zlib.net/) |
| SQLite | 3.53.3 | 3.53.4 | [SQLite downloads](https://www.sqlite.org/download.html) |
| MD4C | 0.5.3 | 0.6.0 | [release](https://github.com/mity/md4c/releases/tag/v0.6.0) |
| OpenSSL | 3.5.7 headers | 3.5.8 LTS | [supported releases](https://www.openssl-library.org/source/) |
| GNU gettext runtime | 0.17 | 1.0, MinGW x64 | [release announcement](https://lists.gnu.org/archive/html/info-gnu/2026-01/msg00007.html) |
| GeoIP C API | patched older snapshot | 1.6.12 | [final release](https://github.com/maxmind/geoip-api-c/releases/tag/v1.6.12) |
| GoogleTest | legacy fused sources | 1.18.0 | [release](https://github.com/google/googletest/releases/tag/v1.18.0) |
| Zstandard | 1.5.7 | unchanged | [releases](https://github.com/facebook/zstd/releases) |
| bzip2 | 1.0.8 | unchanged | [downloads](https://sourceware.org/bzip2/) |
| MiniUPnPc | 2.3.3 | unchanged | [downloads](https://www.miniupnp.tuxfamily.org/files/) |
| libnatpmp | 20230423 | unchanged | [downloads](https://www.miniupnp.tuxfamily.org/files/) |
| libdwarf | 2.3.2 | unchanged | [upstream](https://www.prevanders.net/dwarf.html) |

DWT is maintained in this tree. Boost is no longer a linked dependency.
OpenSSL stays on the supported 3.5 LTS branch; moving to the 4.x API is outside
this maintenance update. GeoIP is an archived legacy API: this updates the reader
without changing DC++'s existing database format to MMDB.

## Integration and platform scope

- zlib retains `NOUNCRYPT` for MiniZip, matching the previous unsupported-encryption
  behavior. Existing compressed fixtures still pass with the updated compressor.
- SQLite and MD4C use upstream source files, with existing application options.
- GeoIP retains Unicode database paths and Windows GEOIDs. IPv6 literals use
  `InetPtonA`; the application uses upstream city-record fields. See
  [the integration patch](../geoip/patches/patch.patch).
- GoogleTest uses the upstream include/src layout through small compatibility
  wrappers. Compression/archive tests now use an immutable copy of the old header
  as sample data, so updating the test framework does not change their fixtures.
- gettext 1.0 and its gnulib support are built statically on **MinGW x64**. Other
  configurations retain the existing runtime until a matching configuration is
  generated and validated. See [runtime build notes](../intl/modern/README.dcpp).
  GNU gettext intentionally ignores `LANGUAGE` when `LC_ALL`/`LC_MESSAGES`/`LANG`
  select `C` or `C.UTF-8`; the translation test sets and restores its own locale.
- OpenSSL public headers and the MinGW x64 configuration are updated. The local
  MinGW x64 static archives were rebuilt and verified against those headers.
  **The archives are ignored build artifacts, not tracked source changes.** Run
  [the build script](../scripts/build-openssl.sh) when preparing a fresh checkout.
  x86 and MSVC binaries were not rebuilt here; rebuild them from 3.5.8 before
  shipping those configurations. See [OpenSSL notes](../openssl/readme.txt).

New DC++ integration files carry `iceman50` copyright. Imported upstream files
retain their original copyright and license notices.

## Source archives

SHA-256 values identify the downloaded archives. zlib, OpenSSL, and gettext were
also checked against upstream published SHA-256 values. SQLite was checked
against its published SHA3-256 value
`628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`.
GitHub tag archive hashes below record the retrieved content; they are not
independent publisher signatures.

| Archive | SHA-256 |
| --- | --- |
| [zlib-1.3.2.tar.gz](https://zlib.net/zlib-1.3.2.tar.gz) | `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16` |
| [sqlite-amalgamation-3530400.zip](https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip) | `1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d` |
| [MD4C v0.6.0](https://github.com/mity/md4c/archive/refs/tags/v0.6.0.tar.gz) | `4d151298125a81da3b2efa2e0eed8bdb7a9318569804e4fa4d7a2375ab83ef70` |
| [openssl-3.5.8.tar.gz](https://github.com/openssl/openssl/releases/download/openssl-3.5.8/openssl-3.5.8.tar.gz) | `a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2` |
| [gettext-1.0.tar.xz](https://ftp.gnu.org/gnu/gettext/gettext-1.0.tar.xz) | `71132a3fb71e68245b8f2ac4e9e97137d3e5c02f415636eb508ae607bc01add7` |
| [GeoIP v1.6.12](https://github.com/maxmind/geoip-api-c/archive/refs/tags/v1.6.12.tar.gz) | `99b119f8e21e94f1dfd6d49fbeed29a70df1544896e76cd456f25e397b07d476` |
| [GoogleTest v1.18.0](https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz) | `6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5` |

## Validation

Built with MinGW-w64 GCC 13.2.0, x64 release:

```text
scons tools=mingw arch=x64 mode=release -j8 test zlib/test build/release-mingw-x64/bin/DCPlusPlus.exe
```

All 189 enabled tests passed (nine explicitly disabled tests/benchmarks), and
the zlib example/minigzip checks passed. Added runtime checks cover OpenSSL header
and binary version agreement, TLS context configuration, UTF-8 gettext catalogs
and plural forms, and GeoIP IPv4/IPv6 lookups through a Unicode Windows path.
The executable's imports require no additional third-party runtime DLL.
No GUI session, x86 build, or MSVC build was exercised by this audit.
