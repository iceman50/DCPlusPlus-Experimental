This folder contains the GeoIP C API 1.6.12, the final upstream release:
https://github.com/maxmind/geoip-api-c/releases/tag/v1.6.12

The legacy library is archived upstream. DC++ still reads its existing legacy
.dat database format; no migration to MMDB is included in this update.

patches/patch.patch records the DC++ changes against libGeoIP in that release:
Windows GEOID conversion, UTF-8/Unicode file paths, and safe IPv6 literal parsing
with InetPtonA. City records now use unmodified upstream fields; dcpp/GeoIP.cpp
converts the country code to the internal country ID when needed.

See docs/dependency-updates.md for the source archive checksum and validation.
