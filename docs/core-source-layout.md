# Core source layout

The core is organized by responsibility. Public C++ names remain in `dcpp`
(including existing nested namespaces); moving a file does not change its API,
ownership, threading, persistence format, or initialization order.

| Directory | Responsibility |
| --- | --- |
| `app` | Startup/shutdown, resource language information and saved session/history state |
| `base` | Exceptions, events, ownership and allocation helpers |
| `system` | Files, streams, threads, synchronization, timers and OS headers |
| `net` | Sockets, TLS and HTTP transport |
| `connectivity` | Connectivity detection, port mapping and HBRI |
| `protocol` | ADC command parsing and serialization |
| `hub` | Hub connections, hub settings and client management |
| `user` | User identities and matching rules |
| `connection` | Peer connection lifecycle |
| `transfer` | Downloads, uploads, throttling, segments and completed transfers |
| `transfer/private` | Direct private transfers, their protocol and storage |
| `queue` | Download queue and scheduling |
| `share` | Share indexing and temporary shares |
| `filelist` | Directory listings and file-list caching |
| `hash` | Hashing, Merkle trees, verification and bloom filters |
| `search` | Search requests/results, ADL search and magnet links |
| `chat` | Messages, private chats, commands and portable formatting data |
| `bbs` | Bulletin boards |
| `favorites` | Favorite hubs, users and groups |
| `settings` | Settings definitions and persistence |
| `geo` | Country databases and archive handling |
| `storage` | SQLite, archives and compression streams |
| `logging` | Log messages and persistence |
| `plugins` | Plugin lifecycle and core plugin API |
| `text` | Text encoding, string matching and XML |
| `util` | Existing general utilities awaiting more focused ownership |

Keep a manager's model and listener headers beside its implementation. Use
project-qualified includes, for example `<dcpp/queue/QueueManager.h>`, so a
dependency's owner is visible. Keep `#include "stdinc.h"` first in implementation
files to preserve precompiled-header compatibility.

The root retains the build entry point, compiler/PCH scaffolding, shared forward
declarations and typedefs, and version-generation files. Both SCons and CMake
discover source/header files recursively. Translation inputs include nested
headers and sources as well.

These directories currently form one core library; they are not independent
libraries or new namespaces. Existing manager dependencies and session/UI-related
data have been preserved. Separating those interfaces is a later refactor. Core
code must not depend on application windows or DWT widgets.
