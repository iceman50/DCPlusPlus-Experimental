# DC++ themes

Copyright (C) 2026 iceman50. Distributed under GPL-3.0-or-later.

This collection contains 25 complete dark-mode themes for DC++. Ten are original dark adaptations inspired by recognizable eras of productivity-suite design, five explore restrained solar, arctic, plum, forest, and amber color families, and ten provide saturated neon and vibrant alternatives.

The names describe visual inspiration only. These themes are independent, are not exact reproductions, include no third-party artwork, and are not affiliated with or endorsed by Microsoft.

Installed and portable builds discover XML files placed directly in `Themes`. To modify a distributed palette, import it, edit it, then save it in the per-user `Themes` directory; a user file with the same filename takes precedence.

Open **Settings > Appearance > Themes** to edit interface, chat, log, transfer, and user-match appearance together. Existing settings remain your initial custom theme. Selecting a preset replaces the draft appearance; click **OK** to apply it, or **Cancel** to discard it. Select **Dark** to use the custom interface palette. Windows high contrast takes precedence for interface colors.

Each supplied theme explicitly defines all 31 content colors: regular and own chat, nicknames, timestamps, links, mentions, history, system messages, log severities, and upload/download bars. Content accents follow the individual theme family, with distinct incoming/own nicknames, tinted mention backgrounds, and separate transfer colors. Every content text/background pair has at least 4.5:1 contrast.

The files also color the default English-named `Favorite users (added by DC++)` and `Operators (added by DC++)` definitions. Renamed, translated, or custom definitions can be styled in **Themes > Users** and saved in a custom theme; association uses exact definition names. No matching rules are supplied or changed.

Every palette provides readable text and selected-text contrast. The seven fields are `Background`, `Surface`, `Text`, `DisabledText`, `Border`, `Accent`, and `HighlightText`, each encoded as `#RRGGBB`.

The neon collection includes Pink Pulse, Electric Purple, Cyber Blue, Voltage Orange, Laser Red, Acid Lime, Aqua Circuit, Hot Magenta, Ultraviolet, and Neon Sunset. Bright accents intentionally use dark selected text where required for contrast.

The preview is an actual RichEdit control using the same renderer as chat. It shows incoming and outgoing messages, timestamps, nicknames, mentions, links, system messages and history, plus a sample of the selected style. Font selection supports bold, italic, underline and strikethrough. **Text effects** also offers underline and strike variants, subscript/superscript, capitalization, outline, shadow, spacing, baseline offset and kerning. Windows RichEdit versions differ in which advanced effects they display; the preview shows the local result. Non-RichEdit controls use the font properties they support. The Text effects button sits beside Select font and opens a live RichEdit preview with grouped checkboxes. Double strikethrough overrides single; script and capitalization alternatives are mutually exclusive. Spacing controls use points, and Reset effects restores normal text. Cancel discards dialog edits.

**Styles / User matching** retains the matching definitions, order and ignore behavior. Their appearance is edited in **Themes > Users**. Theme files associate appearance with exact definition names; they do not contain or change matching rules. Applying a theme without an appearance entry for a definition resets that definition's appearance to inherited values.

Definitions may share a name. Exports preserve their separate appearances using one-based `Occurrence` attributes in theme XML version 3. On import, those entries apply in the current order of definitions with that exact name, including generated definitions hidden from the editor. Keep that relative order when transferring a theme between installations. Existing version 2 name-only entries apply the same appearance to every definition with that name. Exports without duplicate names remain version 2. Live chat uses session-local rule identities, so renaming or reordering definitions does not redirect retained messages to a different rule; these identities and the rules themselves are never exported.

**Import theme** validates a file and loads it into the settings draft. **Save theme** exports the draft to the selected file, even before Settings is applied. Save in the per-user Themes folder to make it available in the preset list; **Open folder** and **Reload themes** manage that list.

Theme XML version 2 retains the seven-field `Palette` and adds `Fonts`, `Colors` and `UserStyles`. Files must be UTF-8 and no larger than 64 KiB. The exporter writes every supported appearance setting. For example:

```xml
<DCPlusPlusTheme Name="Example" Version="2">
  <Palette Background="#202020" Surface="#2D2D30" Text="#F1F1F1"
    DisabledText="#A0A0A0" Border="#494949" Accent="#0078D7"
    HighlightText="#FFFFFF" />
  <Fonts>
    <Font Setting="ChatTextFont" Value="Segoe UI,-12,400,0,1,0,0,0,0,0,0,0" />
  </Fonts>
  <Colors>
    <Color Setting="ChatTextColor" Value="#F1F1F1" />
    <Color Setting="ChatTextBgColor" Value="#202020" />
  </Colors>
  <UserStyles>
    <Style Name="Operators" Font="Segoe UI,-12,700,0,1" TextColor="#78BFFF" />
  </UserStyles>
</DCPlusPlusTheme>
```

The first five font fields remain compatible with existing settings: face, height, weight, italic, charset. Optional fields are underline, strike, effect flags, script, spacing (twips), baseline offset (half-points), and kerning threshold (twips). Use the editor to generate these values. Empty font values inherit the global font. Missing user text/background colors inherit the applicable style.

Existing palette-only files still load, deriving any missing content styles from their palette. The supplied collection now uses version 2 with explicit content colors. After updating the files in an open client, choose **Reload themes**, select the theme again, then click **OK**. A per-user theme with the same filename still overrides the supplied version.
