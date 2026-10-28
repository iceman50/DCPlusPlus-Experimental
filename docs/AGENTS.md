# Documentation placement

Store all new project-authored Markdown documentation in `docs/`, including
source-layout guides, design notes, plans and README-style explanations. Do not
create Markdown documentation in `dcpp/`, `win32/` or other source directories.
Use descriptive filenames in `docs/` and update references when moving a guide.

Preserve bundled third-party licenses and upstream documentation in their
existing locations.

The source-layout guides are `core-source-layout.md` and
`windows-source-layout.md` in this directory.

# Source attribution

Use commit `11f20efae4f1cb7fa4eb2d7343798037328e3dea` as the upstream baseline
when reviewing attribution. Newly authored fork source files must credit only
`iceman50`; modified inherited files must retain their original notices and
add `Copyright (C) 2026 iceman50` immediately beneath Jacek Sieka's notice.
Use the actual contribution year for subsequent work.

Follow renames and extractions when determining a file's origin. A new path
does not make inherited code newly authored. Preserve copyright and license
notices for imported upstream code and bundled dependencies. Keep existing
license grants, exceptions and warranty disclaimers intact.

Before committing source or build-script changes, check the attribution in
every added or modified file. Follow Git history through renames and inspect
the source of extracted code; do not decide authorship from an added-file
status alone. Add a missing fork notice without duplicating an existing one.
Credit only the actual contribution years to `iceman50`, not upstream years.
Apply this rule to project-maintained build and test scripts as well as C/C++.
