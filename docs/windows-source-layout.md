# Windows application source layout

Windows application files are grouped by feature and reusable UI responsibility.
Existing frame and dialog class names are unchanged. Shared helpers extracted
from `WinUtil` use responsibility-specific namespaces. DWT remains the separate
widget library; application-specific behavior belongs here.

| Directory | Responsibility |
| --- | --- |
| `app` | Main window, splash screen, single-instance handling, crash logging, UI startup and DWT translations |
| `frames/base` | Shared frame, status and recent-window infrastructure |
| `frames/hub` | Hub windows, public hubs and hub lists |
| `frames/chat` | Private conversations, chat commands and shared chat text behavior |
| `frames/queue` | Queue window |
| `frames/search` | Search and ADL search windows/dialogs |
| `frames/filelist` | Directory-listing window |
| `frames/transfers` | Transfer view and finished-transfer windows |
| `frames/transfers/private` | Private Send dialog and notifications |
| `frames/favorites` | Favorite hubs and their dialogs |
| `frames/users` | User window, user commands, context-menu actions and user presentation |
| `frames/bbs` | Bulletin boards and posting dialog |
| `frames/tools` | Notepad, statistics, system log, text and advanced configuration windows |
| `settings` | Settings dialog and shared page infrastructure |
| `settings/general` | General preferences |
| `settings/connection` | Connectivity, proxy, certificates and bandwidth |
| `settings/appearance` | Appearance, themes, emoticons, tabs, windows and notifications |
| `settings/transfers` | Download, upload, queue and favorite-directory settings |
| `settings/advanced` | Advanced settings and their supporting dialogs |
| `dialogs` | Dialogs shared across features and file-picker workflows |
| `controls` | Application tables/trees, saved table layouts, filtering, keyboard state and dialog control helpers |
| `appearance` | Themes, icon packs, image lists, country flags, emoticons, fonts, colors and control defaults |
| `shell` | Windows file/URL launching, protocol registration, startup registration and clipboard access |
| `services` | GeoIP updates, help, notifications and application link routing |
| `plugins` | Windows plugin API and plugin information dialog |

Keep feature-specific dialogs with their feature. Use project-qualified includes,
for example `<win32/appearance/IconManager.h>`, instead of adding every feature
directory to the include search path. Implementation files retain
`#include "stdafx.h"` first for precompiled-header compatibility.

The root retains `main.cpp`, PCH/compiler scaffolding, shared forward declarations,
the resource script/header and the build entry point. Artwork remains under
`res/`. SCons and CMake discover nested source/header files, and translation
extraction includes the nested inputs.

All Windows features still build into the same application. These folders are
source ownership boundaries, not independently linked libraries.

`WinUtil` has been removed. Include the component that owns the operation:

| Operation | API |
| --- | --- |
| UI initialization and main-window access | `app/UiServices.h` |
| Theme colors and change notifications | `appearance/AppAppearance.h` |
| Fonts and user-match font cache | `appearance/UiFonts.h` |
| Widget defaults | `appearance/ControlSeeds.h` |
| Icon loading and standard icon sizes | `appearance/IconManager.h` |
| File/user image lists and flag offsets | `appearance/ImageLists.h` |
| Color arithmetic | `appearance/ColorUtil.h` |
| Table layout serialization and sort settings | `controls/TableLayout.h` |
| Table copy menus | `controls/TableActions.h` |
| Common dialog controls | `controls/DialogControls.h` |
| Modifier keys | `controls/KeyState.h` |
| Save/file-list pickers and recent directories | `dialogs/FilePickers.h` |
| Chat commands and selection/path text | `frames/chat/ChatCommands.h`, `frames/chat/ChatText.h` |
| User command parameters | `frames/users/UserCommands.h`, `frames/users/AspectUserCommand.h` |
| User menus and display text | `frames/users/UserActions.h`, `frames/users/UserPresentation.h` |
| Help, notifications and link routing | `services/HelpService.h`, `services/Notifications.h`, `services/LinkActions.h` |
| Shell execution, registration and clipboard | `shell/ShellActions.h`, `shell/ShellIntegration.h`, `shell/ClipboardActions.h` |

`UiServices` coordinates startup in the existing order: appearance, default font,
control seeds, transfer/user-match fonts, semantic colors, image lists, protocol
registration, then help. Individual components own their existing resources and
caches. `IconManager` remains the single icon loader. Link routing and external
protocol policy live in `LinkActions`; shell execution lives in `ShellActions`.

New shared functionality should go to its owning component, without recreating
a catch-all utility header. Feature-specific dialogs stay with their feature.
Broad namespace changes to existing frames and settings pages remain a separate
refactor. Keep project-authored Markdown documentation under `docs/`.
