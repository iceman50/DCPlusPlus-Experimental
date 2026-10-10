# Uncommitted change review - 2026-10-10

Baseline: `7fb2dc1` (`System process information`). Snapshot taken before review edits.

**488 files: 144 modified, 3 deleted, 341 untracked; no staged changes.** Ignored build artifacts are excluded. The tracked diff before review edits was 10,585 insertions and 39,129 deletions across 147 files; this does not include the untracked source trees.

## Review findings

### P2: Duplicate user-match names resolve to the wrong appearance (open)

`dcpp/UserMatchManager.cpp:42` resolves a style by the first matching **name**, although the rule editor permits duplicate names (`win32/UserMatchDlg.cpp:144`, `win32/UserMatchPage.cpp:237`). `match()` correctly finds a matching rule but records only its name; `win32/HtmlToRtf.cpp:301` then skips the embedded style and resolves that name again. With two definitions named Friends, one matching Alice with red text and the other matching Bob with blue text, Bob can render red because the first Friends definition wins even though it does not match Bob.

Export/preview has the opposite collision policy: `win32/ThemeManager.cpp:146` and `win32/ThemePage.cpp:646` overwrite earlier appearances in a map keyed by name. Saving and reapplying a theme can therefore collapse distinct existing styles. Use an unambiguous identity for live resolution and define backward-compatible handling for duplicate names in exported themes. Add coverage for duplicate names with different rules and styles. This finding is based on the code path; it is not covered by the passing tests below. No application-code fix was made in this review.

### P2: Relocated byte-exact fixture lost its LF checkout rule (fixed)

Archive and compression tests now read `test/data/gtest_h_original.txt`, but the explicit `text eol=lf` attribute remained only on `test/gtest.h`. The new path had no eol attribute, allowing CRLF checkouts to break byte/hash comparisons. Added an explicit LF rule for the new immutable fixture in `.gitattributes`. Verified the fixture against the ZIP and zlib payloads and checked its Git attributes.

### Release/build follow-up

- OpenSSL x86/MSVC archives were not rebuilt in the existing dependency update. Only the MinGW x64 configuration was exercised here; the documented rebuild requirement still applies.
- Fresh checkouts need the ignored OpenSSL static libraries built using the supplied script. This review used the existing local artifacts and did not repeat source downloads/checksum verification.
- `git diff --check` reports existing whitespace in the generated revision header, the GeoIP integration patch and imported MD4C header. These were not rewritten as part of this review.
- The 341 untracked files include required production sources, tests and vendored dependency trees. They must accompany the tracked changes when these changes are eventually committed.

## Validation performed

```text
scons tools=mingw arch=x64 mode=release -j8 test zlib/test theme-test dwt/test build/release-mingw-x64/bin/DCPlusPlus.exe
```

- Incremental MinGW x64 release build succeeded. Main executable and several targets were already current; the core runner and several DWT programs were relinked.
- Core suite: **193 tests from 35 suites passed**, with **10 disabled tests/benchmarks** left disabled.
- Executed `ClientFrameTest`, `FramePaintTest`, `RichTextBatchTest`, `RichTextColorTest`, `ToolBarAppearanceTest` and `ThemeTest`; all printed their success result and no stderr. Tests were launched with hidden process windows.
- `zlib/test` was already current in SCons; its standalone programs were not independently rerun during this review.
- Parsed all 25 supplied XML themes; verified the legacy fixture against compressed archive data; parsed both benchmark Python scripts for syntax.
- Logs: `build/uncommitted-review-build.log` and `build/review-<test-name>.log` / `.err`.

This is an initial integration and first-party code review, not an exhaustive line-by-line audit of all imported dependency code. No clean rebuild, x86/MSVC run, Windows 7 run, real-network compression benchmark or interactive accessibility review was performed. Passing automated UI tests do not close the duplicate-name finding.

## Changelog edits made in this review

Updated `changelog-bfe.txt` within its existing 0.8842 section: describe client-area title/menu bars separately from native-dialog flicker fixes; add status-bar/control layout work; mention previous allocation size; add dependency updates with platform scope and benchmark tooling. Kept the existing release heading/date and older committed entries. No entry claims that the duplicate-name bug is fixed.

## Suggested review/commit groups

1. Dependency refresh, licensing, build instructions, OpenSSL script, framework migration and immutable fixtures.
2. Windows file allocation safety, error diagnostics and regression tests.
3. Unified Themes page, font effects, live RichEdit restyling, supplied themes and help (resolve duplicate-name handling before committing).
4. Client-area title/menu bars, native-dialog/toolbar/settings painting, content geometry and UI tests.
5. Opt-in compression benchmark and its preparation/summary scripts.

Some renderer and UI tests span groups 3 and 4. Split only after confirming each proposed commit builds. The generated revision header should be reviewed separately from functional changes.

## Complete baseline inventory

`M` = modified tracked file; `D` = deleted tracked file; `?` = untracked file. Every baseline path appears once below. Descriptions apply to each directory group.

| Group | Modified | Deleted | Untracked | Total |
| --- | ---: | ---: | ---: | ---: |
| Root documentation and metadata | 3 | 0 | 0 | 3 |
| dcpp | 8 | 0 | 2 | 10 |
| win32 | 22 | 2 | 5 | 29 |
| dwt | 19 | 0 | 8 | 27 |
| Themes | 26 | 0 | 0 | 26 |
| help | 6 | 0 | 0 | 6 |
| scripts | 0 | 0 | 3 | 3 |
| docs | 0 | 0 | 1 | 1 |
| geoip | 9 | 1 | 5 | 15 |
| intl | 2 | 0 | 271 | 273 |
| md4c | 5 | 0 | 0 | 5 |
| openssl | 5 | 0 | 3 | 8 |
| sqlite | 2 | 0 | 0 | 2 |
| test | 6 | 0 | 41 | 47 |
| zlib | 31 | 0 | 2 | 33 |

### Root documentation and metadata (3 files)

Build instructions, third-party license notices and the 0.8842 changelog. OpenSSL/gettext platform scope is documented.

| Status | File |
| --- | --- |
| M | `changelog-bfe.txt` |
| M | `Compile.txt` |
| M | `ThirdPartyLicenses.txt` |

### dcpp (10 files)

New FontStyle encoding and RTF effects; per-component user-style source names; chat markup for live restyling; checked Windows file allocation and diagnostic context; GeoIP integration; generated revision identifier.

| Status | File |
| --- | --- |
| M | `dcpp/ChatMessage.cpp` |
| M | `dcpp/Download.cpp` |
| M | `dcpp/File.cpp` |
| ? | `dcpp/FontStyle.cpp` |
| ? | `dcpp/FontStyle.h` |
| M | `dcpp/GeoIP.cpp` |
| M | `dcpp/Style.h` |
| M | `dcpp/UserMatchManager.cpp` |
| M | `dcpp/UserMatchManager.h` |
| M | `dcpp/version-rev-id.inc` |

### win32 (29 files)

StylesPage is replaced by ThemePage and TextEffectsDlg; theme import/export, live previews and retained-message restyling; settings paint/layout changes; dialog/main-window geometry accounts for client chrome; ThemeTest integration target.

| Status | File |
| --- | --- |
| M | `win32/AboutDlg.cpp` |
| M | `win32/AspectChat.h` |
| M | `win32/EmoticonPackDlg.cpp` |
| M | `win32/ExperimentalPage.cpp` |
| M | `win32/ExperimentalPage.h` |
| M | `win32/FavHubGroupsDlg.cpp` |
| M | `win32/GridDialog.cpp` |
| M | `win32/HtmlToRtf.cpp` |
| M | `win32/HtmlToRtf.h` |
| M | `win32/MagnetDlg.cpp` |
| M | `win32/MainWindow.cpp` |
| M | `win32/PluginInfoDlg.cpp` |
| M | `win32/RichTextBox.cpp` |
| M | `win32/RichTextBox.h` |
| M | `win32/SConscript` |
| M | `win32/SettingsDialog.cpp` |
| M | `win32/SettingsDialog.h` |
| M | `win32/StringListDlg.cpp` |
| D | `win32/StylesPage.cpp` |
| D | `win32/StylesPage.h` |
| ? | `win32/test/ThemeTest.cpp` |
| ? | `win32/TextEffectsDlg.cpp` |
| ? | `win32/TextEffectsDlg.h` |
| M | `win32/ThemeManager.cpp` |
| M | `win32/ThemeManager.h` |
| ? | `win32/ThemePage.cpp` |
| ? | `win32/ThemePage.h` |
| M | `win32/UserMatchPage.cpp` |
| M | `win32/WinUtil.cpp` |

### dwt (27 files)

Client-area caption/menu implementation, frame geometry and taskbar integration; native-dialog paint isolation; themed toolbar customization and status-bar buttons; group/page buffering; RichEdit positions, batching and immediate redraw; five new regression programs.

| Status | File |
| --- | --- |
| M | `dwt/include/dwt/doxygen.h` |
| M | `dwt/include/dwt/util/win32/NativeDialogAppearance.h` |
| ? | `dwt/include/dwt/util/win32/NonClientPaint.h` |
| M | `dwt/include/dwt/widgets/Button.h` |
| M | `dwt/include/dwt/widgets/Frame.h` |
| M | `dwt/include/dwt/widgets/GroupBox.h` |
| M | `dwt/include/dwt/widgets/RichTextBox.h` |
| M | `dwt/include/dwt/widgets/ScrolledContainer.h` |
| M | `dwt/src/Taskbar.cpp` |
| M | `dwt/src/util/win32/NativeDialogAppearance.cpp` |
| M | `dwt/src/widgets/AppearanceDraw.cpp` |
| M | `dwt/src/widgets/Button.cpp` |
| ? | `dwt/src/widgets/ClientFrame.cpp` |
| ? | `dwt/src/widgets/ClientFrame.h` |
| M | `dwt/src/widgets/Frame.cpp` |
| M | `dwt/src/widgets/GroupBox.cpp` |
| M | `dwt/src/widgets/MDIFrame.cpp` |
| M | `dwt/src/widgets/Menu.cpp` |
| M | `dwt/src/widgets/Rebar.cpp` |
| M | `dwt/src/widgets/RichTextBox.cpp` |
| M | `dwt/src/widgets/StatusBar.cpp` |
| M | `dwt/src/widgets/ToolBar.cpp` |
| ? | `dwt/test/ClientFrameTest.cpp` |
| ? | `dwt/test/FramePaintTest.cpp` |
| ? | `dwt/test/RichTextBatchTest.cpp` |
| ? | `dwt/test/RichTextColorTest.cpp` |
| ? | `dwt/test/ToolBarAppearanceTest.cpp` |

### Themes (26 files)

All 25 supplied XML palettes now include semantic chat colors and named user appearances; README describes the expanded format and settings workflow.

| Status | File |
| --- | --- |
| M | `Themes/acid-lime.xml` |
| M | `Themes/amber-terminal.xml` |
| M | `Themes/aqua-circuit.xml` |
| M | `Themes/arctic-frost.xml` |
| M | `Themes/cyber-blue.xml` |
| M | `Themes/electric-purple.xml` |
| M | `Themes/evergreen.xml` |
| M | `Themes/hot-magenta.xml` |
| M | `Themes/laser-red.xml` |
| M | `Themes/microsoft-365-fluent-blue-inspired.xml` |
| M | `Themes/neon-pink-pulse.xml` |
| M | `Themes/neon-sunset.xml` |
| M | `Themes/office-2003-classic-blue-inspired.xml` |
| M | `Themes/office-2007-ribbon-black-inspired.xml` |
| M | `Themes/office-2007-ribbon-blue-inspired.xml` |
| M | `Themes/office-2007-ribbon-silver-inspired.xml` |
| M | `Themes/office-2010-azure-inspired.xml` |
| M | `Themes/office-2010-black-gold-inspired.xml` |
| M | `Themes/office-2010-silver-inspired.xml` |
| M | `Themes/office-2013-clean-dark-inspired.xml` |
| M | `Themes/office-2016-colorful-blue-inspired.xml` |
| M | `Themes/plum-night.xml` |
| M | `Themes/README.md` |
| M | `Themes/solar-dusk.xml` |
| M | `Themes/ultraviolet.xml` |
| M | `Themes/voltage-orange.xml` |

### help (6 files)

Settings navigation and descriptions updated for Appearance > Themes and Styles / User matching; obsolete Experimental theme controls removed from help.

| Status | File |
| --- | --- |
| M | `help/dialog_favorite_hub.html` |
| M | `help/dialog_user_matching.html` |
| M | `help/index.html` |
| M | `help/settings_experimental.html` |
| M | `help/settings_styles.html` |
| M | `help/settings_user_matching.html` |

### scripts (3 files)

Pinned OpenSSL source verification/static build script; deterministic benchmark corpus preparation and CSV/throughput summary scripts.

| Status | File |
| --- | --- |
| ? | `scripts/build-openssl.sh` |
| ? | `scripts/prepare-transfer-benchmark.py` |
| ? | `scripts/summarize-transfer-benchmark.py` |

### docs (1 files)

Bundled dependency versions, integration decisions, archive checksums, platform limitations and the earlier dependency audit validation record.

| Status | File |
| --- | --- |
| ? | `docs/dependency-updates.md` |

### geoip (15 files)

Legacy GeoIP C API 1.6.12 source refresh, Unicode Windows paths/GEOIDs and IPv6 integration, integration patch and build metadata. GeoIP_depreciated.c is replaced by the correctly named GeoIP_deprecated.c plus upstream helper sources.

| Status | File |
| --- | --- |
| M | `geoip/ChangeLog` |
| M | `geoip/GeoIP.c` |
| M | `geoip/GeoIP.h` |
| ? | `geoip/GeoIP_deprecated.c` |
| D | `geoip/GeoIP_depreciated.c` |
| M | `geoip/GeoIP_internal.h` |
| M | `geoip/GeoIPCity.c` |
| M | `geoip/GeoIPCity.h` |
| M | `geoip/patches/patch.patch` |
| ? | `geoip/pread.c` |
| ? | `geoip/pread.h` |
| M | `geoip/readme.txt` |
| ? | `geoip/regionName.c` |
| M | `geoip/SConscript` |
| ? | `geoip/timeZone.c` |

### intl (273 files)

MinGW x64 switches to gettext runtime 1.0 with vendored gnulib sources, generated platform headers, source manifest, compatibility helper and licenses. Other configurations retain the existing runtime.

| Status | File |
| --- | --- |
| M | `intl/libintl.h` |
| ? | `intl/modern/bindtextdom.c` |
| ? | `intl/modern/compat.c` |
| ? | `intl/modern/config.h` |
| ? | `intl/modern/dcgettext.c` |
| ? | `intl/modern/dcigettext.c` |
| ? | `intl/modern/dcngettext.c` |
| ? | `intl/modern/dcpp-compat.c` |
| ? | `intl/modern/dgettext.c` |
| ? | `intl/modern/dngettext.c` |
| ? | `intl/modern/eval-plural.h` |
| ? | `intl/modern/explodename.c` |
| ? | `intl/modern/export.h` |
| ? | `intl/modern/finddomain.c` |
| ? | `intl/modern/gettext.c` |
| ? | `intl/modern/gettextP.h` |
| ? | `intl/modern/gmo.h` |
| ? | `intl/modern/gnulib-lib/_Noreturn.h` |
| ? | `intl/modern/gnulib-lib/alloca.h` |
| ? | `intl/modern/gnulib-lib/alloca.in.h` |
| ? | `intl/modern/gnulib-lib/arg-nonnull.h` |
| ? | `intl/modern/gnulib-lib/asnprintf.c` |
| ? | `intl/modern/gnulib-lib/asnwprintf.c` |
| ? | `intl/modern/gnulib-lib/assert.in.h` |
| ? | `intl/modern/gnulib-lib/attribute.h` |
| ? | `intl/modern/gnulib-lib/c++defs.h` |
| ? | `intl/modern/gnulib-lib/c32is-impl.h` |
| ? | `intl/modern/gnulib-lib/c32isalnum.c` |
| ? | `intl/modern/gnulib-lib/c32isalpha.c` |
| ? | `intl/modern/gnulib-lib/c32isblank.c` |
| ? | `intl/modern/gnulib-lib/c32iscntrl.c` |
| ? | `intl/modern/gnulib-lib/c32isdigit.c` |
| ? | `intl/modern/gnulib-lib/c32isgraph.c` |
| ? | `intl/modern/gnulib-lib/c32islower.c` |
| ? | `intl/modern/gnulib-lib/c32isprint.c` |
| ? | `intl/modern/gnulib-lib/c32ispunct.c` |
| ? | `intl/modern/gnulib-lib/c32isspace.c` |
| ? | `intl/modern/gnulib-lib/c32isupper.c` |
| ? | `intl/modern/gnulib-lib/c32isxdigit.c` |
| ? | `intl/modern/gnulib-lib/c32to-impl.h` |
| ? | `intl/modern/gnulib-lib/c32tolower.c` |
| ? | `intl/modern/gnulib-lib/c32width.c` |
| ? | `intl/modern/gnulib-lib/errno.h` |
| ? | `intl/modern/gnulib-lib/errno.in.h` |
| ? | `intl/modern/gnulib-lib/fcntl.h` |
| ? | `intl/modern/gnulib-lib/fcntl.in.h` |
| ? | `intl/modern/gnulib-lib/filename.h` |
| ? | `intl/modern/gnulib-lib/flexmember.h` |
| ? | `intl/modern/gnulib-lib/float+.h` |
| ? | `intl/modern/gnulib-lib/float.c` |
| ? | `intl/modern/gnulib-lib/float.h` |
| ? | `intl/modern/gnulib-lib/float.in.h` |
| ? | `intl/modern/gnulib-lib/fpucw.h` |
| ? | `intl/modern/gnulib-lib/free.c` |
| ? | `intl/modern/gnulib-lib/frexp.c` |
| ? | `intl/modern/gnulib-lib/frexpl.c` |
| ? | `intl/modern/gnulib-lib/fseterr.c` |
| ? | `intl/modern/gnulib-lib/fseterr.h` |
| ? | `intl/modern/gnulib-lib/getcwd-lgpl.c` |
| ? | `intl/modern/gnulib-lib/getlocalename_l-unsafe.c` |
| ? | `intl/modern/gnulib-lib/getlocalename_l-unsafe.h` |
| ? | `intl/modern/gnulib-lib/glthread/lock.c` |
| ? | `intl/modern/gnulib-lib/glthread/lock.h` |
| ? | `intl/modern/gnulib-lib/glthread/once.c` |
| ? | `intl/modern/gnulib-lib/glthread/once.h` |
| ? | `intl/modern/gnulib-lib/glthread/threadlib.c` |
| ? | `intl/modern/gnulib-lib/hard-locale.c` |
| ? | `intl/modern/gnulib-lib/hard-locale.h` |
| ? | `intl/modern/gnulib-lib/intprops-internal.h` |
| ? | `intl/modern/gnulib-lib/inttypes.h` |
| ? | `intl/modern/gnulib-lib/inttypes.in.h` |
| ? | `intl/modern/gnulib-lib/isnan.c` |
| ? | `intl/modern/gnulib-lib/isnand-nolibm.h` |
| ? | `intl/modern/gnulib-lib/isnand.c` |
| ? | `intl/modern/gnulib-lib/isnanf-nolibm.h` |
| ? | `intl/modern/gnulib-lib/isnanf.c` |
| ? | `intl/modern/gnulib-lib/isnanl-nolibm.h` |
| ? | `intl/modern/gnulib-lib/isnanl.c` |
| ? | `intl/modern/gnulib-lib/iswblank.c` |
| ? | `intl/modern/gnulib-lib/iswdigit.c` |
| ? | `intl/modern/gnulib-lib/iswpunct.c` |
| ? | `intl/modern/gnulib-lib/iswxdigit.c` |
| ? | `intl/modern/gnulib-lib/itold.c` |
| ? | `intl/modern/gnulib-lib/lc-charset-dispatch.c` |
| ? | `intl/modern/gnulib-lib/lc-charset-dispatch.h` |
| ? | `intl/modern/gnulib-lib/limits.h` |
| ? | `intl/modern/gnulib-lib/limits.in.h` |
| ? | `intl/modern/gnulib-lib/localcharset.c` |
| ? | `intl/modern/gnulib-lib/localcharset.h` |
| ? | `intl/modern/gnulib-lib/locale.h` |
| ? | `intl/modern/gnulib-lib/locale.in.h` |
| ? | `intl/modern/gnulib-lib/localeconv.c` |
| ? | `intl/modern/gnulib-lib/localename-environ.c` |
| ? | `intl/modern/gnulib-lib/localename-table.c` |
| ? | `intl/modern/gnulib-lib/localename-table.h` |
| ? | `intl/modern/gnulib-lib/localename-unsafe.c` |
| ? | `intl/modern/gnulib-lib/localename.c` |
| ? | `intl/modern/gnulib-lib/localename.h` |
| ? | `intl/modern/gnulib-lib/math.c` |
| ? | `intl/modern/gnulib-lib/math.h` |
| ? | `intl/modern/gnulib-lib/math.in.h` |
| ? | `intl/modern/gnulib-lib/mbchar.c` |
| ? | `intl/modern/gnulib-lib/mbchar.h` |
| ? | `intl/modern/gnulib-lib/mbiterf.c` |
| ? | `intl/modern/gnulib-lib/mbiterf.h` |
| ? | `intl/modern/gnulib-lib/mbrtoc32.c` |
| ? | `intl/modern/gnulib-lib/mbrtowc-impl-utf8.h` |
| ? | `intl/modern/gnulib-lib/mbrtowc-impl.h` |
| ? | `intl/modern/gnulib-lib/mbrtowc.c` |
| ? | `intl/modern/gnulib-lib/mbsinit.c` |
| ? | `intl/modern/gnulib-lib/mbsnlen.c` |
| ? | `intl/modern/gnulib-lib/mbszero.c` |
| ? | `intl/modern/gnulib-lib/mbtowc-lock.c` |
| ? | `intl/modern/gnulib-lib/mbtowc-lock.h` |
| ? | `intl/modern/gnulib-lib/memchr.c` |
| ? | `intl/modern/gnulib-lib/printf-args.c` |
| ? | `intl/modern/gnulib-lib/printf-args.h` |
| ? | `intl/modern/gnulib-lib/printf-frexp.c` |
| ? | `intl/modern/gnulib-lib/printf-frexp.h` |
| ? | `intl/modern/gnulib-lib/printf-frexpl.c` |
| ? | `intl/modern/gnulib-lib/printf-frexpl.h` |
| ? | `intl/modern/gnulib-lib/printf-parse.c` |
| ? | `intl/modern/gnulib-lib/printf-parse.h` |
| ? | `intl/modern/gnulib-lib/pthread-once.c` |
| ? | `intl/modern/gnulib-lib/pthread.h` |
| ? | `intl/modern/gnulib-lib/pthread.in.h` |
| ? | `intl/modern/gnulib-lib/sched.h` |
| ? | `intl/modern/gnulib-lib/sched.in.h` |
| ? | `intl/modern/gnulib-lib/search.h` |
| ? | `intl/modern/gnulib-lib/search.in.h` |
| ? | `intl/modern/gnulib-lib/setlocale-fixes.c` |
| ? | `intl/modern/gnulib-lib/setlocale-fixes.h` |
| ? | `intl/modern/gnulib-lib/setlocale-lock.c` |
| ? | `intl/modern/gnulib-lib/setlocale_null-unlocked.c` |
| ? | `intl/modern/gnulib-lib/setlocale_null.c` |
| ? | `intl/modern/gnulib-lib/setlocale_null.h` |
| ? | `intl/modern/gnulib-lib/signbitd.c` |
| ? | `intl/modern/gnulib-lib/signbitf.c` |
| ? | `intl/modern/gnulib-lib/signbitl.c` |
| ? | `intl/modern/gnulib-lib/size_max.h` |
| ? | `intl/modern/gnulib-lib/stdckdint.h` |
| ? | `intl/modern/gnulib-lib/stdckdint.in.h` |
| ? | `intl/modern/gnulib-lib/stddef.h` |
| ? | `intl/modern/gnulib-lib/stddef.in.h` |
| ? | `intl/modern/gnulib-lib/stdint.h` |
| ? | `intl/modern/gnulib-lib/stdint.in.h` |
| ? | `intl/modern/gnulib-lib/stdio-consolesafe.c` |
| ? | `intl/modern/gnulib-lib/stdio-impl.h` |
| ? | `intl/modern/gnulib-lib/stdio-read.c` |
| ? | `intl/modern/gnulib-lib/stdio-write.c` |
| ? | `intl/modern/gnulib-lib/stdio.h` |
| ? | `intl/modern/gnulib-lib/stdio.in.h` |
| ? | `intl/modern/gnulib-lib/stdlib.c` |
| ? | `intl/modern/gnulib-lib/stdlib.h` |
| ? | `intl/modern/gnulib-lib/stdlib.in.h` |
| ? | `intl/modern/gnulib-lib/streq.h` |
| ? | `intl/modern/gnulib-lib/string.c` |
| ? | `intl/modern/gnulib-lib/string.h` |
| ? | `intl/modern/gnulib-lib/string.in.h` |
| ? | `intl/modern/gnulib-lib/strncpy.c` |
| ? | `intl/modern/gnulib-lib/struniq.h` |
| ? | `intl/modern/gnulib-lib/sys/types.h` |
| ? | `intl/modern/gnulib-lib/sys_types.in.h` |
| ? | `intl/modern/gnulib-lib/thread-optim.h` |
| ? | `intl/modern/gnulib-lib/time.h` |
| ? | `intl/modern/gnulib-lib/time.in.h` |
| ? | `intl/modern/gnulib-lib/tsearch.c` |
| ? | `intl/modern/gnulib-lib/uchar.h` |
| ? | `intl/modern/gnulib-lib/uchar.in.h` |
| ? | `intl/modern/gnulib-lib/unicase.h` |
| ? | `intl/modern/gnulib-lib/unicase.in.h` |
| ? | `intl/modern/gnulib-lib/unicase/simple-mapping.h` |
| ? | `intl/modern/gnulib-lib/unicase/tolower.c` |
| ? | `intl/modern/gnulib-lib/unicase/tolower.h` |
| ? | `intl/modern/gnulib-lib/unictype.h` |
| ? | `intl/modern/gnulib-lib/unictype.in.h` |
| ? | `intl/modern/gnulib-lib/unictype/bitmap.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_alnum.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_alnum.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_alpha.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_alpha.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_blank.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_blank.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_cntrl.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_cntrl.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_digit.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_digit.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_graph.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_graph.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_lower.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_lower.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_print.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_print.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_punct.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_punct.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_space.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_space.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_upper.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_upper.h` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_xdigit.c` |
| ? | `intl/modern/gnulib-lib/unictype/ctype_xdigit.h` |
| ? | `intl/modern/gnulib-lib/uninorm.h` |
| ? | `intl/modern/gnulib-lib/uninorm.in.h` |
| ? | `intl/modern/gnulib-lib/unistd.c` |
| ? | `intl/modern/gnulib-lib/unistd.h` |
| ? | `intl/modern/gnulib-lib/unistd.in.h` |
| ? | `intl/modern/gnulib-lib/unitypes.h` |
| ? | `intl/modern/gnulib-lib/unitypes.in.h` |
| ? | `intl/modern/gnulib-lib/uniwidth.h` |
| ? | `intl/modern/gnulib-lib/uniwidth.in.h` |
| ? | `intl/modern/gnulib-lib/uniwidth/cjk.h` |
| ? | `intl/modern/gnulib-lib/uniwidth/width.c` |
| ? | `intl/modern/gnulib-lib/uniwidth/width0.h` |
| ? | `intl/modern/gnulib-lib/uniwidth/width2.h` |
| ? | `intl/modern/gnulib-lib/vasnprintf.c` |
| ? | `intl/modern/gnulib-lib/vasnprintf.h` |
| ? | `intl/modern/gnulib-lib/vasnwprintf.c` |
| ? | `intl/modern/gnulib-lib/vasnwprintf.h` |
| ? | `intl/modern/gnulib-lib/verify.h` |
| ? | `intl/modern/gnulib-lib/warn-on-use.h` |
| ? | `intl/modern/gnulib-lib/wchar.h` |
| ? | `intl/modern/gnulib-lib/wchar.in.h` |
| ? | `intl/modern/gnulib-lib/wctype-h.c` |
| ? | `intl/modern/gnulib-lib/wctype.h` |
| ? | `intl/modern/gnulib-lib/wctype.in.h` |
| ? | `intl/modern/gnulib-lib/wcwidth.c` |
| ? | `intl/modern/gnulib-lib/wgetcwd-lgpl.c` |
| ? | `intl/modern/gnulib-lib/windows-initguard.h` |
| ? | `intl/modern/gnulib-lib/windows-mutex.c` |
| ? | `intl/modern/gnulib-lib/windows-mutex.h` |
| ? | `intl/modern/gnulib-lib/windows-once.c` |
| ? | `intl/modern/gnulib-lib/windows-once.h` |
| ? | `intl/modern/gnulib-lib/windows-recmutex.c` |
| ? | `intl/modern/gnulib-lib/windows-recmutex.h` |
| ? | `intl/modern/gnulib-lib/windows-rwlock.c` |
| ? | `intl/modern/gnulib-lib/windows-rwlock.h` |
| ? | `intl/modern/gnulib-lib/wmemcpy-impl.h` |
| ? | `intl/modern/gnulib-lib/wmemcpy.c` |
| ? | `intl/modern/gnulib-lib/wmemset-impl.h` |
| ? | `intl/modern/gnulib-lib/wmemset.c` |
| ? | `intl/modern/gnulib-lib/wprintf-parse.c` |
| ? | `intl/modern/gnulib-lib/wprintf-parse.h` |
| ? | `intl/modern/gnulib-lib/xsize.c` |
| ? | `intl/modern/gnulib-lib/xsize.h` |
| ? | `intl/modern/hash-string.c` |
| ? | `intl/modern/hash-string.h` |
| ? | `intl/modern/intl-compat.c` |
| ? | `intl/modern/intl-exports.c` |
| ? | `intl/modern/l10nflist.c` |
| ? | `intl/modern/langprefs.c` |
| ? | `intl/modern/libgnuintl.h` |
| ? | `intl/modern/libgnuintl.in.h` |
| ? | `intl/modern/libintl.h` |
| ? | `intl/modern/loadinfo.h` |
| ? | `intl/modern/loadmsgcat.c` |
| ? | `intl/modern/localealias.c` |
| ? | `intl/modern/log.c` |
| ? | `intl/modern/ngettext.c` |
| ? | `intl/modern/os2compat.c` |
| ? | `intl/modern/os2compat.h` |
| ? | `intl/modern/osdep.c` |
| ? | `intl/modern/plural-exp.c` |
| ? | `intl/modern/plural-exp.h` |
| ? | `intl/modern/plural.c` |
| ? | `intl/modern/plural.h` |
| ? | `intl/modern/printf.c` |
| ? | `intl/modern/README.dcpp` |
| ? | `intl/modern/SConscript` |
| ? | `intl/modern/setlocale.c` |
| ? | `intl/modern/sources.txt` |
| ? | `intl/modern/textdomain.c` |
| ? | `intl/modern/version.c` |
| M | `intl/SConscript` |

### md4c (5 files)

MD4C 0.6.0 parser and entity sources, public header and license refresh.

| Status | File |
| --- | --- |
| M | `md4c/entity.c` |
| M | `md4c/entity.h` |
| M | `md4c/LICENSE.md` |
| M | `md4c/md4c.c` |
| M | `md4c/md4c.h` |

### openssl (8 files)

3.5.8 public headers and MinGW x64 configuration, platform include support and rebuild notes. Static archives are ignored artifacts and are outside this inventory.

| Status | File |
| --- | --- |
| ? | `openssl/include/openssl/__DECC_INCLUDE_EPILOGUE.H` |
| ? | `openssl/include/openssl/__DECC_INCLUDE_PROLOGUE.H` |
| ? | `openssl/include/openssl/configuration-mingw-x64.h` |
| M | `openssl/include/openssl/opensslconf-mingw-x64.h` |
| M | `openssl/include/openssl/opensslv.h` |
| M | `openssl/include/openssl/ssl.h` |
| M | `openssl/include/openssl/x509err.h` |
| M | `openssl/readme.txt` |

### sqlite (2 files)

SQLite amalgamation and public header move from 3.53.3 to 3.53.4.

| Status | File |
| --- | --- |
| M | `sqlite/sqlite3.c` |
| M | `sqlite/sqlite3.h` |

### test (47 files)

GoogleTest 1.18.0 upstream source layout behind compatibility wrappers; immutable legacy header fixture; updated compression/archive tests; Windows file-resize/allocation and dependency runtime checks; opt-in compression benchmark.

| Status | File |
| --- | --- |
| ? | `test/benchtransfercompression.cpp` |
| ? | `test/data/gtest_h_original.txt` |
| ? | `test/googletest/include/gtest/gtest-assertion-result.h` |
| ? | `test/googletest/include/gtest/gtest-death-test.h` |
| ? | `test/googletest/include/gtest/gtest-matchers.h` |
| ? | `test/googletest/include/gtest/gtest-message.h` |
| ? | `test/googletest/include/gtest/gtest-param-test.h` |
| ? | `test/googletest/include/gtest/gtest-printers.h` |
| ? | `test/googletest/include/gtest/gtest-spi.h` |
| ? | `test/googletest/include/gtest/gtest-test-part.h` |
| ? | `test/googletest/include/gtest/gtest-typed-test.h` |
| ? | `test/googletest/include/gtest/gtest.h` |
| ? | `test/googletest/include/gtest/gtest_pred_impl.h` |
| ? | `test/googletest/include/gtest/gtest_prod.h` |
| ? | `test/googletest/include/gtest/internal/custom/gtest-port.h` |
| ? | `test/googletest/include/gtest/internal/custom/gtest-printers.h` |
| ? | `test/googletest/include/gtest/internal/custom/gtest.h` |
| ? | `test/googletest/include/gtest/internal/custom/README.md` |
| ? | `test/googletest/include/gtest/internal/gtest-death-test-internal.h` |
| ? | `test/googletest/include/gtest/internal/gtest-filepath.h` |
| ? | `test/googletest/include/gtest/internal/gtest-internal.h` |
| ? | `test/googletest/include/gtest/internal/gtest-param-util.h` |
| ? | `test/googletest/include/gtest/internal/gtest-port-arch.h` |
| ? | `test/googletest/include/gtest/internal/gtest-port.h` |
| ? | `test/googletest/include/gtest/internal/gtest-string.h` |
| ? | `test/googletest/include/gtest/internal/gtest-type-util.h` |
| ? | `test/googletest/README.dcpp` |
| ? | `test/googletest/src/gtest-all.cc` |
| ? | `test/googletest/src/gtest-assertion-result.cc` |
| ? | `test/googletest/src/gtest-death-test.cc` |
| ? | `test/googletest/src/gtest-filepath.cc` |
| ? | `test/googletest/src/gtest-internal-inl.h` |
| ? | `test/googletest/src/gtest-matchers.cc` |
| ? | `test/googletest/src/gtest-port.cc` |
| ? | `test/googletest/src/gtest-printers.cc` |
| ? | `test/googletest/src/gtest-test-part.cc` |
| ? | `test/googletest/src/gtest-typed-test.cc` |
| ? | `test/googletest/src/gtest.cc` |
| ? | `test/googletest/src/gtest_main.cc` |
| M | `test/gtest-all.cpp` |
| M | `test/gtest.h` |
| M | `test/SConscript` |
| M | `test/testarchive.cpp` |
| ? | `test/testdependencyruntime.cpp` |
| ? | `test/testdownloadallocation.cpp` |
| M | `test/testfile.cpp` |
| M | `test/testzutils.cpp` |

### zlib (33 files)

zlib/MiniZip 1.3.2 sources, headers, upstream examples and notices. Build explicitly preserves unsupported encrypted ZIP behavior with NOUNCRYPT.

| Status | File |
| --- | --- |
| M | `zlib/ChangeLog` |
| M | `zlib/compress.c` |
| M | `zlib/crc32.c` |
| M | `zlib/deflate.c` |
| M | `zlib/deflate.h` |
| M | `zlib/gzguts.h` |
| M | `zlib/gzlib.c` |
| M | `zlib/gzread.c` |
| M | `zlib/gzwrite.c` |
| M | `zlib/inffast.c` |
| M | `zlib/inffixed.h` |
| M | `zlib/inflate.c` |
| M | `zlib/inflate.h` |
| M | `zlib/inftrees.c` |
| M | `zlib/inftrees.h` |
| ? | `zlib/ints.h` |
| M | `zlib/ioapi.c` |
| M | `zlib/ioapi.h` |
| M | `zlib/iowin32.c` |
| M | `zlib/iowin32.h` |
| ? | `zlib/LICENSE` |
| M | `zlib/README` |
| M | `zlib/SConscript` |
| M | `zlib/test/example.c` |
| M | `zlib/test/minigzip.c` |
| M | `zlib/trees.c` |
| M | `zlib/uncompr.c` |
| M | `zlib/unzip.c` |
| M | `zlib/unzip.h` |
| M | `zlib/zconf.h` |
| M | `zlib/zlib.h` |
| M | `zlib/zutil.c` |
| M | `zlib/zutil.h` |

## Files changed by this review

- `.gitattributes`: explicit LF checkout rule for the relocated immutable fixture.
- `changelog-bfe.txt`: current implementation and missing dependency/benchmark entries.
- `docs/uncommitted-review.md`: this inventory, findings and validation record.

The SCons build regenerated the revision include with the same current revision. No changes were staged or committed.
