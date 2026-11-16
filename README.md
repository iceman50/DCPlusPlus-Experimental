# DC++ Experimental

DC++ Experimental is a fork of [DC++](https://dcplusplus.sourceforge.io/) 0.884, the open-source Windows client for
the Direct Connect (NMDC) and Advanced Direct Connect (ADC) networks. It adds new ADC protocol features, a dark
theme and appearance customization, an SQLite hash database, and extensive protocol hardening.

It is a testing ground, not a stable release. Expect rough edges, and back up your settings before you try it.

Downloads (x64 portable and installer): [GitHub releases](https://github.com/iceman50/DCPlusPlus-Experimental/releases)

## Using it alongside DC++

DC++ Experimental keeps DC++'s storage conventions, so it can replace an existing installation:

- An installed copy uses the same AppData folders as DC++.
- A portable copy keeps its settings beside the executable.

On first start it migrates your hash data into a new SQLite database (`HashStore.sqlite3`). The old `HashIndex.xml`
and `HashData.dat` files are left unchanged, so DC++ can still use them.

Several new protocol features are off by default or opt-in. Most of the new limits and switches are under
**Settings > Experimental**, and each option is documented in the help.

## Differences from DC++

### Protocol features

| Feature | What it adds |
|---|---|
| **CCPM** direct private messages | Encrypted client-to-client private chat, with typing and message-seen notices (CPMI), coordinated window closing, configurable automatic reconnects, and an offline outbox for messages to users who are away. |
| **DMSG** | ADC direct messages (PMs routed only to the recipient, without echo). |
| **Hub failover** | ADC hubs can announce failover addresses, and favorite hubs can list their own. The client reconnects through them when the primary hub is unreachable. |
| **MCN1** | Multiple connections per user for downloads and uploads, with fair per-user upload slots and a dedicated connection for small transfers. |
| **HBRI** | Validates IPv4 and IPv6 connectivity through hubs on the other address family (based on AirDC++). |
| **RTF0 rich text** | CommonMark/GitHub-style Markdown chat in hubs and private chats, rendered safely, with a Markdown editor and live preview. Supports magnet attachments and inline images. |
| **BBS0 bulletin boards** | Persistent hub boards with posts, replies and withdrawals, verified by TTH, plus a BBS workspace for browsing, posting and subscribing. |
| **Private Send** (PFT1, off by default) | Direct, consent-based file offers to one recipient over TLS 1.2+, with pause/resume, integrity checks and protected saving outside your shares. |
| **Zstandard** (opt-in) | ZST1/ZS1 compressed transfers with fallback to the legacy compression, and an indexed Zstandard file-list cache (`.xml.zst`). |

### Sharing, hashing and files

- **Per-hub shares:** each favorite hub can use its own share profile.
- **Temporary chat shares:** files dropped into chat are shared only on that hub or private chat, with optional expiry.
- **SQLite hash database** with batching, integrity checks and optimize/compact actions, plus an SQLite share-tree cache that speeds up startup with large shares.
- **Multi-threaded Tiger Tree hashing** with a configurable worker limit.
- **Long Windows paths** (beyond `MAX_PATH`) work for hashing, sharing, downloads and dialogs.
- **AirDC++-compatible file lists:** file and folder timestamps with a sortable Date column, and recursive partial lists.
- **GeoLite2 country lookups** for IPv4 and IPv6 through libmaxminddb, replacing the legacy GeoIP library, with country flags and in-client database updates.

### Interface

- **Dark mode and themes:** a Windows 7-compatible dark theme that also follows the Windows setting and high-contrast mode. Ships with 31 themes, a theme editor for colors, fonts and text effects, and a theme selector on the main toolbar.
- **Custom title bars and menus** in dark and custom appearances, with themed native dialogs and scrollbars.
- **Emoticon and icon packs:** `.dcemo` and `.dcico` packages, with 3 emoticon packs and 5 icon packs bundled, including a dark icon set chosen automatically.
- **Chat:** separate colors and fonts for timestamps, nicknames, your own messages and mentions, and smoother updates in busy chats.
- **Windows and tabs:** a redesigned Network Statistics window, a System Log with severity levels, an in-tab hub login prompt, per-favorite tab names, touchscreen panning, and adjustable user-list icon sizes.

### Security and robustness

- Strict ADC command, state and parameter validation for hub, client-to-client and UDP traffic.
- Bounded protocol queues, plus flood and rate limits for incoming connections and UDP packets.
- Hardened encrypted search (SUDP) and SOCKS5 handling, and validation of hub-supplied endpoints.
- Safer settings files: values too long to load back are trimmed when saving.
- Several fixes from DC++ 0.884 have been ported, as noted in the changelog.

### Building and dependencies

- Boost has been removed in favor of the C++ standard library.
- The primary toolchain is MinGW-w64 13.2 (x64) with SCons. An experimental CMake build supports Visual Studio, and optional x86-64-v2/v3 targets are available.
- Bundled libraries: OpenSSL 3.5.8, zlib 1.3.2, SQLite 3.53.4, Zstandard 1.5.7, md4c 0.6.0, libmaxminddb 1.14.1 and the gettext 1.0 runtime.
- Tests live under `test/<component>` and use GoogleTest 1.18.0.

## Building

See [Compile.txt](Compile.txt) for the required tools. With them on your `PATH`:

```
scons tools=mingw mode=release arch=x64
python test/run.py dcpp
```

The first command builds `build/release-mingw-x64/bin/DCPlusPlus.exe`. The second runs the core tests; you can also
pass `dwt`, `win32` or `all`.

## Changelogs

- [changelog-bfe.txt](changelog-bfe.txt): changes in DC++ Experimental.
- [changelog.txt](changelog.txt): upstream DC++ history.

## License

DC++ Experimental is licensed under the GNU General Public License v3 or later, with an exception permitting linking
with OpenSSL. See [License.txt](License.txt). Bundled third-party components and their licenses are listed in
[ThirdPartyLicenses.txt](ThirdPartyLicenses.txt).

DC++ is copyright © 2001-2025 Jacek Sieka and the DC++ contributors. The modifications in this fork are copyright ©
2026 iceman50.

## AI assistance

Parts of this project's code were written, reviewed and tested with the help of Claude, an AI model by Anthropic.

The project also uses ChatGPT Codex 5.5 by OpenAI for debugging, code checking, modifying scripts and porting code.
