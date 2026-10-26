# Windows application source layout

Windows application files are grouped by feature and reusable UI responsibility.
Existing C++ names and namespaces are unchanged. DWT remains the separate widget
library; application-specific behavior belongs here.

| Directory | Responsibility |
| --- | --- |
| `app` | Main window, splash screen, single-instance handling and crash logging |
| `frames/base` | Shared frame, status and recent-window infrastructure |
| `frames/hub` | Hub windows, public hubs and hub lists |
| `frames/chat` | Private conversations and shared chat behavior |
| `frames/queue` | Queue window |
| `frames/search` | Search and ADL search windows/dialogs |
| `frames/filelist` | Directory-listing window |
| `frames/transfers` | Transfer view and finished-transfer windows |
| `frames/transfers/private` | Private Send dialog and notifications |
| `frames/favorites` | Favorite hubs and their dialogs |
| `frames/users` | User window and shared user actions |
| `frames/bbs` | Bulletin boards and posting dialog |
| `frames/tools` | Notepad, statistics, system log, text and advanced configuration windows |
| `settings` | Settings dialog and shared page infrastructure |
| `settings/general` | General preferences |
| `settings/connection` | Connectivity, proxy, certificates and bandwidth |
| `settings/appearance` | Appearance, themes, emoticons, tabs, windows and notifications |
| `settings/transfers` | Download, upload, queue and favorite-directory settings |
| `settings/advanced` | Advanced settings and their supporting dialogs |
| `dialogs` | Dialogs shared across features |
| `controls` | Application tables/trees, filtering and rich-text editing |
| `appearance` | Themes, icon packs, country flags and emoticons |
| `shell` | Shell integration |
| `services` | UI-facing service coordination, including GeoIP updates |
| `plugins` | Windows plugin API and plugin information dialog |
| `util` | Existing shared application utilities and user-command helpers |

Keep feature-specific dialogs with their feature. Use project-qualified includes,
for example `<win32/appearance/IconManager.h>`, instead of adding every feature
directory to the include search path. Implementation files retain
`#include "stdafx.h"` first for precompiled-header compatibility.

The root retains `main.cpp`, PCH/compiler scaffolding, shared forward declarations,
the resource script/header and the build entry point. Artwork remains under
`res/`. SCons and CMake discover nested source/header files, and translation
extraction includes the nested inputs.

This is a source organization change. All Windows features still build into the
same application. Subsystem namespaces, narrower interfaces and splitting
`WinUtil` are separate follow-up work.
