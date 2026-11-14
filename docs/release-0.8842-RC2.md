# DC++ Experimental 0.8842 RC2

Second release candidate for 0.8842, published November 14, 2026. This prerelease
updates RC1 with live tab sizing, consistent application fonts, and theme and
chat repaint fixes.

## Changes since RC1

- Resize open tabs immediately with the width slider in Settings > Appearance >
  Tabs. OK saves the width; Cancel restores the saved width. Selection, custom
  tab labels and page layout are preserved. Drawing/style changes still require
  a restart, and the settings help explains their behavior with dark themes.
- Apply the global font to controls, title bars, menus and tooltips, including
  existing windows and newly opened dialogs. Explicit content fonts are retained,
  with font scaling when moving between displays.
- Use Windows interface colors in Light mode while preserving the saved custom
  palette for Dark mode. Follow Windows switches palettes with the system
  preference, and the global font remains active in every mode.
- Keep theme editing controls reachable in smaller settings windows and with
  larger fonts. Global text-color edits also update the interface text color.
- Repaint completed visible chat batches immediately without losing selection
  or reading position, and avoid repainting hidden chats.
- Avoid unnecessary settings-page repainting when a dialog gains or loses focus.

## Downloads

| Package | Download |
| --- | --- |
| Windows x64 portable ZIP, including GeoLite2 Country | [Portable ZIP](https://github.com/iceman50/DCPlusPlus-Experimental/releases/download/DCPlusPlus-Experimental-0.8842-RC2/DCPlusPlus-Experimental-0.8842-RC2-x64-Portable.zip) |
| Windows x64 installer | [Setup EXE](https://github.com/iceman50/DCPlusPlus-Experimental/releases/download/DCPlusPlus-Experimental-0.8842-RC2/DCPlusPlus-Experimental-0.8842-RC2-x64-Setup.exe) |
| SHA-256 checksums | [SHA256SUMS.txt](https://github.com/iceman50/DCPlusPlus-Experimental/releases/download/DCPlusPlus-Experimental-0.8842-RC2/SHA256SUMS.txt) |

The portable ZIP stores settings and runtime data in its own `profile` folder.
Extract the complete archive into a writable folder and close other DC++
Experimental instances before launching it. Both packages include matching debug
symbols. Existing installations should retain their own profile when upgrading.

See the [complete changelog](https://github.com/iceman50/DCPlusPlus-Experimental/blob/DCPlusPlus-Experimental-0.8842-RC2/changelog-bfe.txt).

## Testing

Validation covers the MinGW x64 release build, live tab widths and row layout,
tab switching and chat repainting, font inheritance and DPI scaling, theme
switching and settings layout, custom frames and appearance rendering.

RC1's touchscreen panning enhancements remain enabled on Windows 8.1 and later.
Physical touchscreen validation remains pending; the portable package includes
`TOUCHSCREEN-TESTING.md`. Private Send remains experimental and disabled by default.

Please [report problems](https://github.com/iceman50/DCPlusPlus-Experimental/issues)
with the build number, Windows version, display scaling, and reproduction steps.

## Country database

The portable package retains RC1's unmodified GeoLite2 Country snapshot built
November 6, 2026, with its attribution and terms in `GeoLite2-NOTICE.txt`. This
product includes GeoLite2 data created by [MaxMind](https://www.maxmind.com).
Configure database updates with your own account; no personal configuration or
account credentials are included in the release packages.
