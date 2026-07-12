# Fork Changes

This fork is maintained by [neungzap](https://github.com/neungzap) and adds the
features below on top of upstream [dail8859/NotepadNext](https://github.com/dail8859/NotepadNext).
The code remains licensed under GPLv3 (see [LICENSE](LICENSE)), same as upstream.

## Compare Files

A new **Edit > Compare Files...** dialog for diffing two pieces of text side by side.

- Load either side from an already-open tab (works for unsaved tabs like "New1"/"New2"
  with no file on disk) or from a file, with a Refresh button to reload the latest content.
- Diffing is line-based with inline word/character-level highlighting inside modified
  lines, so a single-word change doesn't highlight the whole line.
- A minimap strip next to each pane shows, at a glance, where the differences are in
  the whole file, and jumps to a spot when clicked; it also shows the current scroll
  viewport.
- Copy buttons always copy the original, unpadded text (not the blank-line-padded
  alignment view), plus a Find bar (Left/Right/Both scope) with Cmd+F.
- Background/text/added/removed color pickers, Light/Dark theme presets, and a
  Reset to Defaults button. Matches the app's configured editor font.
- Cmd+C/Cmd+V/Cmd+F work correctly inside the dialog even though macOS shares one
  menu bar across all windows.

## Customizable Editor Appearance

New controls in **Preferences**:

- Editor background, text, and current-line-highlight colors (with Light/Dark theme
  presets and a Reset to Defaults button). Applies live to all open tabs and persists
  across restarts.
- Adjustable line spacing (extra pixels above/below each line) -- useful for scripts
  like Thai where tone marks and vowels need a bit more vertical room.
- Window width/height controls with an Apply button and Reset to Defaults.
- A "Show status bar" toggle alongside the window size controls.

## Trackpad Pinch-to-Zoom

Two-finger pinch on a trackpad now zooms the editor text in/out, in addition to the
existing Ctrl+scroll wheel shortcut.
