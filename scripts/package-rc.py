# Copyright (C) 2026 iceman50
from datetime import datetime, timezone
import argparse
from pathlib import Path
import hashlib
import importlib.util
import re
import struct
import subprocess
import zipfile
import xml.etree.ElementTree as ET
import zlib

root = Path(__file__).resolve().parent.parent
binary = root / 'build/release-mingw-x64/bin'
version = re.search(r'#define VERSIONSTRING "([^"]+)"', (root / 'dcpp/core/version.h').read_text()).group(1)
assert '-RC' in version, 'Expected a release-candidate version'
parser = argparse.ArgumentParser(description='Package a clean portable release candidate without personal data.')
parser.add_argument('--allow-dirty', action='store_true', help='Build a local review artifact, not a publishable release')
args = parser.parse_args()
dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root).strip())
assert not dirty or args.allow_dirty, 'Commit source changes before release packaging, or use --allow-dirty for local review'
output = root / 'dist' / version
output.mkdir(parents=True, exist_ok=True)
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
build = re.search(r'DCPP_BUILD_NUMBER (\d+)', (root / 'build/release-mingw-x64/dcpp/version-build.inc').read_text()).group(1)
database = binary / 'GeoLite2-Country.mmdb'
validation = subprocess.check_output([str(root / 'build/validate-mmdb.exe'), str(database)], text=True)
epoch = int(re.search(r'build_epoch=(\d+)', validation).group(1))
date = datetime.fromtimestamp(epoch, timezone.utc)
assert 0 <= (datetime.now(timezone.utc) - date).days <= 30, 'Database snapshot is stale'

spec = importlib.util.spec_from_file_location('debug_check', root / 'test/win32/test_debug_companion.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
link = module.pe_sections(binary / 'DCPlusPlus.exe')['.gnu_debuglink']
end = link.index(b'\0')
assert link[:end] == b'DCPlusPlus.pdb'
crc = 0
with (binary / 'DCPlusPlus.pdb').open('rb') as source:
    for chunk in iter(lambda: source.read(1024 * 1024), b''):
        crc = zlib.crc32(chunk, crc)
assert struct.unpack_from('<I', link, (end + 4) & ~3)[0] == crc

files = {name: binary / name for name in ('DCPlusPlus.exe', 'DCPlusPlus.pdb')}
for name in ('DCPlusPlus.chm', 'cshelp.rtf'):
    assert (binary / name).is_file(), f'{name} is missing; build with i18n=1 help=1'
    files[name] = binary / name
if (binary / 'locale').is_dir():
    for path in sorted((binary / 'locale').rglob('*')):
        if path.is_file(): files[path.relative_to(binary).as_posix()] = path
else:
    print('No locale folder; the package will contain English only.')
for name in ('changelog-bfe.txt', 'COPYING.TXT', 'License.txt', 'ThirdPartyLicenses.txt'):
    files[name] = root / name
for directory in ('Themes', 'Emoticons', 'IconPacks'):
    for path in sorted((root / directory).rglob('*')):
        if path.is_file(): files[path.relative_to(root).as_posix()] = path
files['TOUCHSCREEN-TESTING.md'] = root / 'docs/touchscreen-testing.md'
files['GeoLite2-Country.mmdb'] = database
with database.open('rb') as source:
    database_hash = hashlib.file_digest(source, 'sha256').hexdigest()
notice = f'''GeoLite2 Country database
Build: {date.isoformat()}
SHA256: {database_hash}
The database is an unmodified MaxMind GeoLite2-Country snapshot.

This product includes GeoLite2 data created by MaxMind, available from
https://www.maxmind.com.

Use of the database is subject to the GeoLite End User License Agreement:
https://www.maxmind.com/en/geolite/eula
That agreement incorporates Creative Commons Attribution-ShareAlike 4.0:
https://creativecommons.org/licenses/by-sa/4.0/
The same applicable terms govern recipients of this database. Preserve this
notice and attribution when redistributing it.

Use current data. Replace and destroy superseded data within 30 days after a
new database is released, as required by the GeoLite agreement. Do not use it
to identify a household, individual, or street address. Data is supplied as-is.

Configure database updates in the application using your own MaxMind account.
This package contains no MaxMind account ID or license key.
'''
readme = f'''DC++ Experimental {version}, build {build} - Windows x64 portable release candidate
Source commit: {revision}{" (uncommitted local review build)" if dirty else ""}
Compiler: MinGW-w64 / MSVCRT / Release
Release: https://github.com/iceman50/DCPlusPlus-Experimental/releases/tag/DCPlusPlus-Experimental-{version}

Extract the complete ZIP into a new writable folder and run DCPlusPlus.exe.
Close other Experimental instances first. Settings and runtime data stay in
the same folder as DCPlusPlus.exe. No personal configuration, queues, or credentials are
bundled. Keep DCPlusPlus.pdb beside the executable for crash diagnostics.

The package includes a GeoLite2 Country snapshot dated {date.date()}; country
lookups are enabled by default. See GeoLite2-NOTICE.txt for attribution and
update terms. Configure your own account for subsequent database updates.

Touchscreen panning enhancements activate only on Windows 8.1 and later.
See TOUCHSCREEN-TESTING.md for the physical-device checklist. Automated pan
and version-gate checks passed; physical-device validation remains pending.

This is {version}, not the final 0.8842 release. Private Send remains experimental
and disabled by default. See changelog-bfe.txt for the complete changes.
'''
extra = {'README.txt': readme + r'''
Settings follow the standard DC++ layout. The portable package stores data
beside DCPlusPlus.exe. The installer defaults to the existing DC++ settings
in %APPDATA%\DC++ and runtime data in %LOCALAPPDATA%\DC++.
For an executable-only replacement, retain your existing dcppboot.xml and
supporting data. An explicit ConfigPath is honored as in DC++.
''' , 'GeoLite2-NOTICE.txt': notice,
         'dcppboot.xml': (root / 'dcppboot.xml').read_text(encoding='utf-8')}
boot = ET.fromstring(extra['dcppboot.xml'])
assert boot.findtext('LocalMode', '1') == '1', 'Portable builds must use upstream local mode'
assert boot.findtext('ConfigPath', '.').replace(chr(92), '/').rstrip('/') == '.', 'Portable builds must default to the executable directory'

destination = output / f'DCPlusPlus-Experimental-{version}-x64-Portable.zip'
temporary = destination.with_suffix('.partial.zip')
assert not destination.exists() and not temporary.exists()
with zipfile.ZipFile(temporary, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
    for name, path in files.items(): archive.write(path, name)
    for name, text in extra.items(): archive.writestr(name, text)
with zipfile.ZipFile(temporary) as archive:
    assert archive.testzip() is None
    assert set(archive.namelist()) == set(files) | set(extra)
    assert not any(name.lower().startswith('profile/') for name in archive.namelist())
    assert not any(name.lower() in {'dcplusplus.xml', 'favorites.xml', 'queue.xml', 'hashstore.sqlite3', 'plugins.xml'} for name in archive.namelist())
    for name, path in files.items():
        with archive.open(name) as packed, path.open('rb') as source:
            assert hashlib.file_digest(packed, 'sha256').digest() == hashlib.file_digest(source, 'sha256').digest(), name
temporary.rename(destination)
(output / 'BUILD-INFO.txt').write_text(readme + '\nDatabase validation:\n' + validation, encoding='utf-8')
print(destination)
print(f'Build {build}, source {revision}, {destination.stat().st_size / 1024 / 1024:.1f} MiB')

