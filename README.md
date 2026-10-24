# Tom's WM

A Wayland compositor that reads twm (Tom LaStrange, 1987) for today:
squeezed title tabs, 1 px frames, an Icon Manager instead of a panel, root
menus, outline moves, and one plain-text `~/.twmrc`. The binary is `twm`.

![Tom's WM: two windows, the Icon Manager and the Panner](docs/shots/m4-desktop.png)

- **twm's grammar, kept.** Title tabs as wide as their names, an iconify box
  and a resize box, click to place a new window, moves and resizes as
  outlines, menus from the bare desktop.
- **Nothing that moves for show.** No blur, shadows, rounded corners, panel,
  dock or tray; no animation over 80 ms. Focus is shown by a hatch, not
  only by color.
- **Calm.** Windows never steal focus; they ask, and `M-u` takes you there.
  Quiet desktops hold their requests until you visit.
- **Honest about privacy.** A window capturing the screen says so in its
  tab, always. Consent prompts appear in a frame no app can imitate, and
  synthetic input never reaches them.
- **The config is the interface.** Everything is a line in `~/.twmrc`,
  reloaded on save; a bad file keeps the old one running and says why. The
  Settings window edits that file and shows you the diff.
- **Small and portable.** C17 on wlroots 0.20, with XWayland, for Linux and
  FreeBSD.

- Design spec: [docs/DESIGN.md](docs/DESIGN.md) (requirements are cited by ID, e.g. `FRM-3`)
- Progress: [docs/PROGRESS.md](docs/PROGRESS.md)

## Build

C17, meson, wlroots 0.20. If the system has no wlroots 0.20, meson builds a
pinned copy (and the newer libwayland, libdrm and pixman it needs) as
subprojects.

```sh
meson setup build
ninja -C build
./build/src/twm --version
```

Dependencies: wlroots 0.20, wayland, wayland-protocols, xkbcommon, pixman,
fcft (fontconfig, freetype, harfbuzz), libinput; Xwayland for X11 clients;
sd-bus for the portal backend and notifications (libsystemd on Linux, basu on
FreeBSD; `-Dportal=disabled` to build without). The UI font is IBM Plex Mono.

## Run

- As your desktop: `sudo meson install -C build --skip-subprojects` puts `twm`,
  `twm-session` and a `wayland-sessions` entry ("Tom's WM") in `/usr/local`;
  pick it in your display manager, or log in on a text console and run
  `twm-session`. Logs: `journalctl -t twm`.
- Nested: run `./build/src/twm` inside a Wayland or X11 session.
- On a TTY: run it from a virtual console (needs seatd or logind).
- Headless: `./build/src/twm --headless 1920x1080` (used by the tests).

`twm -s <cmd>` runs a command once the compositor is up. `M` is the logo key.

| Input | Does |
|-------|------|
| click | places a new window (its outline hangs from the pointer); B2-drag sizes it |
| B1 on a title | raise and focus; drag to move |
| B2 on a title, `M-B1` in a window | move (outline, commits on release) |
| B1/B2 on the resize box, `M-B3` in a window | resize |
| `M-S-m`, `M-r` | move / resize the focused window with the arrow keys |
| `Esc`, `Return` | cancel / commit an outline |
| `M-space` | raise or lower |
| B1 on the bare desktop, `M-m` | the root menu |
| type at the Icon Manager | find a window (or a command); Return goes there, `M-Return` brings it here |
| `M-1`…`M-9`, `M-S-1`…`M-S-9` | switch desktop / send the focused window |
| `M-i`, the iconify box | iconify (B1 on its icon brings it back) |
| B3 on a title | squeeze to the tab and back |
| B2-drag a window onto another's title | group them as tabs; B1 on a tab brings it up, `M-Tab` cycles, B3 folds |
| B2-drag a tab off its row | it stands alone again |
| `M-;` then a letter | hint mode: jump to the window with that letter |
| `M-n` | recent messages |
| `M-u` | go to the window that asked for attention; again to come back |
| B3 on a desktop button in the Icon Manager | make that desktop quiet (its windows' requests wait until you visit) |
| `M-z`, `M-S-z` | undo / redo the focused window's last move, resize, iconify, squeeze, send |
| `M-S-h` | the history view |
| `M-,` | Settings: change `~/.twmrc` with the diff in view; Write applies it, `M-z` undoes |
| a finger on the touchscreen | touch, for apps that take it; elsewhere the pointer: tap to click, drag to move |
| hold `M` while dragging | move freely; otherwise outlines snap to edges and thirds |
| B3 while placing | fit the new window into the empty space around the pointer |
| `M-Return` | `foot` |
| `M-S-q` | quit |

Focus follows the pointer once it rests on a window for 80 ms. All of this is
configurable.

## Configure

Everything lives in `~/.twmrc` (or `$TWMRC`, or `twm -f file`); the syntax and
every key are in [DESIGN.md §5](docs/DESIGN.md#5-twmrc-key-registry). A small
example:

```
Accent        #7FD1AE
FocusPolicy   sloppy
SqueezeTitle  { "xclock" right  "*" left }
"M-Return"    : !"foot"
Button3 = title : f.squeeze
```

Or press `M-,` for the Settings window: each control names the key it
writes, the pending diff is always visible, and Write saves and applies it
(comments and order kept). Save the file by hand and twm reloads it. A file with errors is never applied: the
old config keeps running and the error appears on the desktop.

```sh
twm --check ~/.twmrc
```

```sh
twm --reload
```

Window positions are remembered per app id in `$XDG_STATE_HOME/twm/geometry`.

The touchpad is set in the same file (or under Settings → Touchpad & touch):

```
TouchpadTap            on
TouchpadNaturalScroll  on
TouchpadSpeed          0.3
TouchpadClick          clickfinger
Touchscreen            on
```

## Privacy and prompts

A window never takes the focus because it asked: it is marked, and `M-u`
goes there. A window whose app is capturing the screen says `SCREEN` in its
tab and in the Icon Manager, and a capture with no window writes a line on
the desktop; neither can be turned off.

Consent dialogs appear in twm's *trusted frame*, which no app can draw: a
lock in the tab, a dotted band inside an accent border, the desktop dimmed
behind it. While it is open all input goes to it, and synthetic input (remote
tools, virtual keyboards) goes nowhere.

- Portal access dialogs: install twm (it ships `twm.portal` and
  `twm-portals.conf` for xdg-desktop-portal).
- Screen sharing through xdg-desktop-portal-wlr: copy
  `data/xdg-desktop-portal-wlr.conf` to `~/.config/xdg-desktop-portal-wlr/twm`
  so xdpw asks with `twm --choose`.
- Notifications (`notify-send`) become desktop lines, if no other
  notification daemon runs (`Notifications off` to leave them alone).

## Test

```sh
meson test -C build --suite twm
```

Screenshot tests run twm on the headless backend with the pixman renderer and
drive it over Wayland (virtual pointer in, screencopy out). Screenshots land in
`build/tests/*.png`.

## License

MIT, see [LICENSE](LICENSE).
