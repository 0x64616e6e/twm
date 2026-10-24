# Tom's WM — design specification

Tom's WM is a Wayland compositor that reads twm (Tom LaStrange, 1987) for
today. Binary `twm`, config `~/.twmrc`.

This file is the source of truth for code. Every behavior has a stable ID
(`FRM-3`, `FOC-2`, …). Code, tests, commits and `PROGRESS.md` cite these IDs.
IDs are never renumbered: a dropped requirement is struck through and kept, a
new one is appended at the end of its section.

Each requirement carries its milestone and the board it comes from:
`[M1 · B0]`. "Decided" marks a choice the boards leave open, with the reason.

---

## 0. Sources

The design canvas: <https://claude.ai/artifact/JTyZNmdsY6JT3kGf68Sqcz>

| ID | Board | Size | Scale |
|----|-------|------|-------|
| B0 | Main — "Desktop — phosphor" | 1920×1080 | **1:1, canonical** |
| B1 | Anatomy of a frame | 1440×1080 | big frame 2×; "States" row 1:1 |
| B2 | 01 Tabs, finally | 1440×900 | ≈1.15× (30 px tabs) |
| B3 | 02 Outlines that think | 1440×900 | ≈1.15× |
| B4 | 03 Hint mode | 1440×900 | ≈1.15× |
| B5 | 04 The Icon Manager is the switcher | 1440×900 | ≈1.15× |
| B6 | 05 The trusted frame | 1440×900 | ≈1.15× |
| B7 | 06 Calm by default | 1440×900 | ≈1.15× |
| B8 | 07 Settings, as a file | 1440×900 | ≈1.15× |
| B9 | 08 twmfetch | 1440×900 | ≈1.15× (28 px tab) |
| B10 | 09 The mark | 1440×900 | — |
| B11 | 10 The greeter | 1920×1080 | **1:1** (30 px tab) |
| B12 | 11 Greeter — states | 1440×1080 | ≈1.15× |
| B13 | 12 A real mark | 1440×900 | — |

B9–B13 come with the owner's design document *twmfetch — Design Document*
(2026-10-24), read with them: §3 FET, CARD, SHOT, SHR, MRK and GRT.

**Measurement rule.** B0 is drawn at real size and wins every conflict, then
B1. B2–B8 draw tabs at 30 px where B0 uses 26 px; sizes read from them are
scaled by 26/30 and rounded, unless a requirement gives the value outright.

---

## 1. Principles

- **P-1** twm's grammar: squeezed title tabs (iconify box left, resize box
  right), 1 px frames, an Icon Manager instead of a panel, root menus on a
  bare-desktop click, outline moves and click-to-place, one plain-text config.
- **P-2** No blur, no shadows, no rounded corners, no gradients on chrome.
- **P-3** No animation longer than 80 ms. The default is no animation.
- **P-4** No panel, no dock, no tray, no toasts.
- **P-5** Focus is shown by texture (the highlight hatch) as well as color.
- **P-6** The config is the interface: anything settable has a `.twmrc` key.
- **P-7** A bad config never takes the desktop down: the old one keeps running
  and the error goes on the root line.
- **P-8** Every pointer action has a key. (B1, "New in 2026")

---

## 2. Tokens

All sizes are logical pixels (see GEN-3 for scaling).

### 2.1 Color — theme `phosphor`

| Token | Value | Used for |
|-------|-------|----------|
| `ground` | `#0D0E0B` | root, readout box, input fields, trusted band base |
| `surface` | `#121310` | bodies of compositor-drawn windows (Icon Manager, menu, Panner, history, prompts) |
| `surface-sunk` | `#0F100D` | settings diff pane |
| `graphite` | `#23251F` | unfocused tab, menu header |
| `hairline` | `#2F312A` | unfocused frame, separators, readout border |
| `hairline-strong` | `#3A3D34` | menu border, input border, secondary buttons, trusted inner border |
| `rule-faint` | `#1E201B` | row rules inside lists |
| `guide-faint` | `#2A2C26` | screen-thirds guides during outline ops |
| `mini` | `#5A5D53` | window miniatures in the Panner |
| `ink` | `#E8E6DF` | primary text, outline |
| `ink-2` | `#C9C7BF` | secondary text, list rows |
| `muted` | `#9A9C93` | unfocused tab text and glyphs, labels |
| `dim` | `#85887C` | tertiary text, shortcuts, iconified rows |
| `accent` | `#FFB000` | focus, selection, urgency (configurable) |
| `on-accent` | `#0E0F0C` | text and glyphs on accent |
| `ok` | `#9FD8B8` | "config ok", diff additions |
| `alert` | `#FF8A65` | capture markers, diff removals |

Accent presets (B0 props, B8 swatches): amber `#FFB000` (default), mint
`#7FD1AE`, blue `#8AB4FF`, coral `#FF8A65`. Any `#RRGGBB` is accepted.

Derived colors are stored as alpha and blended at draw time, because the
accent can change:

| Token | Definition |
|-------|-----------|
| `on-accent-sep` | `on-accent` at 30 % — button separators on a focused tab |
| `hatch-ink` | `on-accent` at 55 % — hatch lines on accent |
| `accent-wash` | `accent` at 10 % — focused row in lists |
| `accent-wash-soft` | `accent` at 8 % — pending history row, selected prompt choice |
| `accent-wash-mini` | `accent` at 12 % — focused window miniature in the Panner |
| `outline-ink` | `ink` at 85 % — outline rectangle and title line |
| `outline-thirds` | `ink` at 35 % — rule-of-thirds lines |
| `weave-dot` | `ink` at 7 % — root weave |
| `trusted-dot` | `ink` at 32 % — trusted band weave |
| `wordmark` | `ink` at 7 % — root wordmark |
| `ok-wash`, `alert-wash` | `ok` / `alert` at 8 % — diff line backgrounds |

### 2.2 Type

- UI face: **IBM Plex Mono**, weights 400 / 500 / 600 (Regular, Medium,
  SemiBold). Fallback: fontconfig `monospace` (asked for as a family list,
  `IBM Plex Mono,monospace`, so a missing Plex never lands on the default
  proportional sans). With no font at all twm still runs: text is not
  drawn and the log says so once.
- Display face: **Instrument Serif Italic**, used only for the root wordmark.
  Fallback: `serif:italic`; if neither resolves the wordmark is skipped.
- Tracking (letter-spacing) is applied by the text module from glyph advances.

| Role | Size / weight | Tracking | Color |
|------|---------------|----------|-------|
| `title-focused` | 12 / 600 | 0 (see FRM-11) | on-accent |
| `title` | 12 / 500 | 0 | muted (urgent: accent, 600) |
| `caps` (status line, section heads) | 11 / 400, uppercase | 1.5 | dim / muted |
| `outline-label` | 11 / 400 | 0 | ink |
| `readout` | 15 / 600 | 0.5 | accent |
| `readout-hint` | 11 / 400 | 0 | muted |
| `menu` | 13 / 400 | 0 | ink; shortcut dim |
| `menu-header` | 12 / 400, uppercase | 1 | muted |
| `list` (IM rows) | 12 / 400 | 0 | ink-2 |
| `filter-input` | 13 / 400 | 0 | ink |
| `hint-letter` | 13 / 600 | 0 | on-accent |
| `root-line` | 12 / 400 | 0 | see ROOT-5 |
| `wordmark` | Instrument Serif Italic 168 | −2 | wordmark |

### 2.3 Metrics

| Name | Value | Source |
|------|-------|--------|
| `TitleHeight` | 26 (range 22–34) | B0; B8 slider |
| button box | `TitleHeight` × `TitleHeight` | B0 |
| button glyph | 12 × 12, centered | B0 |
| button separator | 1 | B0 |
| title padding (window) | 14 each side | B0 |
| title padding (utility) | 12 each side | B0 Icon Manager, Panner |
| hatch | 72 wide, inset 6 top and bottom, 8 gap before resize box | B0 |
| `BorderWidth` | 1 | B0, B1 |
| urgent dash | 3 on, 3 off | B1 (1 px CSS dashed) |
| outline label offset | (10, 5) | B0 |
| readout padding | 10 vertical, 16 horizontal, 4 between lines | B0 |
| Icon Manager | width 236, margin 24 from output edges | B0 |
| IM desktop row | padding 10, gap 6, button height 28, 4 per row | B0 |
| IM list row | height 30, side padding 12, gap 10, marker 8 × 8 (iconified: 8 × 2) | B0 |
| IM divider | 1 px, margin 6 vertical 12 horizontal | B0 |
| IM filter row | input height 33, row padding 10 | B5 scaled |
| IM filter result row | height 35 | B5 scaled |
| menu | width 260, header 32, item 30, side padding 14, separator margin 4/14, list padding 4 | B0 |
| menu header hatch | 60 × 10 | B0 |
| Panner | width 236, margin 24, padding 10, gap 6, cell height 62, 2 columns | B0 |
| root icon | box 56, glyph 28, label gap 8, slot width 72, slot gap 20, origin (72, H − 140) | B0 |
| history row | height 33, padding 12 | B3 scaled |
| trusted band | 10 | B6 (kept, not scaled: it is a security affordance) |
| capture dot | 7 diameter | B6 |
| `SnapDistance` | 12 | decided |

### 2.4 Patterns

- **Highlight hatch.** Horizontal 1 px lines with a 3 px period (1 on, 2 off),
  the first line on the first row of the hatch box. Lines are `hatch-ink` over
  the tab color. A 26 px tab gives a 72 × 14 box with 5 lines. On graphite
  (menu header) the lines are `hairline-strong`.
- **Root weave.** A 6 × 6 tile with a 2 × 2 dot at (2,2)–(3,3) in
  `weave-dot` over `ground`. Toggle: `RootWeave`.
- **Trusted weave.** A 4 × 4 tile, same 2 × 2 dot, in `trusted-dot` over
  `ground`. Never configurable, never available to clients.
- **Dash.** 3 on, 3 off; the phase runs continuously clockwise around the
  frame from its top-left corner.
- **Glyphs** (12 px grid, 1 px strokes, pixel coordinates inclusive):
  - iconify: square outline (0,0)–(11,11), filled square (4,4)–(7,7).
  - resize: square outlines (0,0)–(11,11), (0,0)–(7,7), (0,0)–(3,3) — twm's
    nested-corner box.
  - lock (trusted tab): body outline (1,6)–(11,12), shackle arc from (3,6) up
    to y = 1 and down to (9,6).
  - quiet (bell with a slash): B7; drawn on a 14 px grid.

---

## 3. Requirements

### FRM — frames and tabs

- **FRM-1** `[M1 · B0 B1]` Every managed toplevel (xdg-shell and XWayland,
  excluding popups and override-redirect windows) gets a frame: a
  `BorderWidth` border around the client and a title tab on top. Nothing else
  decorates a window.
- **FRM-2** `[M1 · B0]` The tab sits on the frame's top edge: its bottom edge
  touches the border's top edge, its left edge lines up with the frame's
  outer left edge (for `SqueezeTitle left`). Frame outer size =
  client + 2·`BorderWidth` wide, client + 2·`BorderWidth` + `TitleHeight` tall.
  *Decided (after M7):* a client's own shadow or rounded corners outside its
  window geometry are cut off (P-2); its popups are not, so menus may reach
  past its edge.
- **FRM-3** `[M1 · B0]` Tab layout, left to right: iconify box, 1 px
  separator, 14 padding, title text, 14 padding, *(focused only: 72 hatch,
  8 gap)*, 1 px separator, resize box.
- **FRM-4** `[M1 · B0 B1 B8]` Squeezed titles: the tab is only as wide as its
  content; the rest of the top edge shows whatever lies below. `SqueezeTitle`
  sets the tab's justification per window: `left` (default), `center`,
  `right`, or `off`. With `off` the tab spans the frame width and, when
  focused, the hatch fills the space between the title and the resize box
  (classic twm).
- **FRM-5** `[M1]` *Decided:* when the content is wider than the frame, the
  title is cut with "…" so the tab never exceeds the frame. Buttons are never
  dropped. Reason: keeps both boxes reachable on narrow windows.
- **FRM-6** `[M1 · B0]` A fixed-size client (minimum size equals maximum size,
  both axes, non-zero) has no resize box and no right separator. (B0 xclock.)
- **FRM-7** `[M1 · B0 B1]` Focused: tab `accent`; title `title-focused`;
  glyphs `on-accent`; separators `on-accent-sep`; hatch shown; border `accent`.
- **FRM-8** `[M1 · B0 B1]` Unfocused: tab `graphite`; title `title`; glyphs
  `muted`; separators `hairline`; no hatch; border `hairline`.
- **FRM-9** `[M1 · B1]` Urgent (and not focused): tab `graphite` with a 1 px
  inset `accent` outline; title and glyphs `accent` at weight 600;
  separators `accent`; border 1 px **dashed** `accent`. Gaining focus clears
  urgency and the frame turns focused.
- **FRM-10** `[M1 · B1 B8]` The hatch appears exactly when the window holds
  keyboard focus. `TitleHighlight off` hides it (B8 offers the toggle);
  default `on`.
- **FRM-11** `[M1 · B0]` The tab grows by 80 px (hatch + gap) on focus and
  shrinks back on blur. The anchored side stays put (left edge for `left`,
  center for `center`, right edge for `right`). No animation. *Decided:* B0
  draws unfocused tabs without the hatch space, so the width change is part of
  the design, not a side effect. *Decided (M1):* B0's 0.3 px letter-spacing
  on the focused title is dropped, so the title itself doesn't move when focus
  changes; Plex Mono's weights share one advance width, so only the hatch and
  its gap appear. 0.3 px is below a device pixel at scale 1.
- **FRM-12** `[M1 · B0 B1]` Pointer bindings on the frame (defaults, all
  rebindable): B1 on iconify box → `f.iconify`; B1 or B2 drag on resize box →
  `f.resize`; B2 on title → `f.move`; B3 on title → `f.squeeze`; B1 click on
  title → `f.raise`; B1 drag on title past 4 px → `f.move`.
- **FRM-13** `[M4 · B0 B1]` Iconify unmaps the window from the desktop and
  lists it in the Icon Manager's iconified section and as an icon on the root
  (ROOT-9). *Decided (M4):* root icons show for the current desktop only, like
  the windows they stand for; deiconifying puts the window back on it.
- **FRM-14** `[M4 · B0]` *Decided:* `f.squeeze` on a single window shades it
  (client hidden, frame reduced to its tab; again restores), as in ctwm's
  `f.squeeze`. On a tab group it folds the tabs instead (GRP-6). The client
  is not reconfigured.
- **FRM-15** `[M1]` Server-side decorations always: xdg-decoration answers
  `server_side` to every request, and the KDE server-decoration protocol
  defaults to server mode (GTK 3 honors it). Clients that ignore both still
  get a frame. `NoTitle { "app-id-pattern" … }` opts windows out.
- **FRM-16** `[M1]` XWayland toplevels get identical frames. Override-redirect
  windows are unframed. Transients are framed.
- **FRM-17** `[M1]` Title source: xdg `title` / `_NET_WM_NAME` / `WM_NAME`;
  if empty, the app id / `WM_CLASS`; if empty, `untitled`.
- **FRM-18** `[M3 · B0]` Utility frames (Icon Manager, Panner, history, log)
  have a title-only tab with 12 padding and no buttons; the same state colors
  apply. *Decided (M3):* they are drawn by twm but are ordinary windows:
  focus, stacking, moves, bindings and remembered geometry work as for any
  client.
- **FRM-19** `[owner, after M7]` A close box, the owner's addition to B0's
  tab: a title-height box with a cross (two 1 px diagonals, no curves) after
  the hatch and before the resize box, behind a 1 px separator, in the tab's
  colors; fixed-size windows end with it. A group row has one, for the tab
  in front. B1 on it closes (`f.delete`: the app is asked, and may ask to
  save); B3 kills (`f.destroy`, twm's: a Wayland client's connection is cut,
  as XKillClient did; an X11 app's process gets SIGKILL, since every X client
  shares XWayland's connection). Context `close` for button bindings.
  `CloseBox off` gives B0's tab back. Utility frames and trusted prompts
  have none. Keys: `M-w` closes, `M-S-w` kills; the root menu has `close`
  and `kill` (they ask for a window, MNU-5). A kill writes a root line.
- **FRM-19** `[M1 · P-2]` Frames are axis-aligned and pixel-snapped, opaque,
  with no shadow, radius or blur.

### FOC — focus

- **FOC-1** `[M1 · B0 B7]` `FocusPolicy sloppy` (default): keyboard focus
  follows the pointer into windows and stays put when the pointer moves onto
  the root.
- **FOC-2** `[M1 · B7]` Intent delay: keyboard focus moves to a window only
  after the pointer has stayed inside that window's frame continuously for
  `FocusDelay` ms (default 80; 0 = immediate). Leaving earlier cancels it, so
  a fast crossing (B7: 34 ms) never moves focus.
- **FOC-3** `[M1]` The delay restarts when the pointer enters a different
  frame. Motion inside the same frame does not restart it.
- **FOC-4** `[M1]` *Decided:* only keyboard focus waits. Pointer focus
  (`wl_pointer` enter/motion) goes to the surface under the pointer at once, so
  hover and scrolling work immediately. Reason: the delay protects typing, not
  pointing.
- **FOC-5** `[M1]` Any button press in a window focuses it immediately.
- **FOC-6** `[M1]` Focus never raises (twm). Raising is `f.raise` (B1 on the
  title, `M-space` toggles raise/lower). `RaiseOnClick on` also raises on a
  click in the client; default `off`.
- **FOC-7** `[M2]` `FocusPolicy click`: focus changes only on click.
- **FOC-8** `[M1]` No focus changes while a grab is active (move, resize,
  place, menu, hint mode, filter).
- **FOC-9** `[M1]` *Decided:* a new window gets focus through the click that
  places it (OUT-12), which is user intent. With non-interactive placement a
  new window takes focus only if it presents a valid xdg-activation token;
  otherwise it maps unfocused. *Decided (M2):* programs twm starts from a
  binding or menu get a token in `XDG_ACTIVATION_TOKEN` (and
  `DESKTOP_STARTUP_ID`), so a window the user just asked for may take focus;
  a window restored from memory (OUT-16) is not focused otherwise. Reason: a slow-starting app must not grab
  keystrokes meant for another window (B7 "never steals focus").
- **FOC-10** `[M1]` When the focused window goes away, focus goes to the
  window under the pointer, else the most recently focused window on the
  current desktop, else nothing.

### OUT — outlines: move, resize, place

- **OUT-1** `[M1 · B0 B1 B3]` Move and resize use an outline by default
  (`OpaqueMove off`): the window stays still while an outline shows the target
  geometry, and the client is configured once, on release. No resize storms.
- **OUT-2** `[M1 · B0]` Outline drawing (above everything): a 1 px
  `outline-ink` rectangle of the frame's outer box; a 1 px `outline-ink` line
  `TitleHeight` below its top; rule-of-thirds lines (1 px, `outline-thirds`)
  at ⅓ and ⅔ of the width and height; the window's name in `outline-label` at
  (10, 5).
- **OUT-3** `[M1 · B0 B3]` Geometry readout, centered in the outline: `ground`
  box, 1 px `hairline` border, padding per §2.3; line 1 `W×H+X+Y` in
  `readout`; line 2 a hint in `readout-hint`, omitted when empty. *Decided:*
  if the outline is smaller than the readout, the readout sits 8 px below
  the outline.
- **OUT-4** `[M1 · B0 B3]` Geometry is the frame's outer box in logical
  pixels, X and Y relative to the top-left of the output holding the outline's
  top-left. Signs are explicit (`+380`, `-12`); unlike X11 geometry, a minus
  is a negative coordinate, not an offset from the far edge. For clients with
  size increments (XWayland `WM_NORMAL_HINTS`) the hint line adds the size in
  cells, e.g. `80×24 cells`.
- **OUT-5** `[M1]` The outline and readout follow every pointer motion and
  are drawn on the next output frame.
- **OUT-6** `[M1 · B1 B3]` Releasing the button that started the operation
  commits it. `Esc` cancels and leaves the window unchanged. `Return` commits
  a keyboard operation.
- **OUT-7** `[M1 · P-8]` During any outline operation the arrow keys move
  (or resize) by 1 px, Shift+arrow by 10 px.
- **OUT-8** `[M1]` Starting a move: B2 on the title (immediate), B1 drag on
  the title (4 px threshold), `M-B1` anywhere in the window, `f.move` from a
  menu (MNU-5), or `M-S-m` for the focused window (keyboard).
- **OUT-9** `[M1]` *Decided:* starting a resize: dragging the resize box moves
  the bottom-right corner by the pointer delta; `M-B3` in the window picks by
  the 3×3 grid under the pointer (corner cell → that corner, edge cell → that
  edge, center → bottom-right); `M-r` resizes the focused window with the
  arrow keys. Reason: twm's "cross an edge to pick it" needs pointer warping,
  which does not work nested.
- **OUT-10** `[M1]` The outline honours the client's minimum and maximum size;
  the readout shows the clamped values. A frame is never narrower than its
  tab's buttons.
- **OUT-11** `[M2 · B8]` `OpaqueMove on`: the window moves live; resize sends
  configures live, at most one unacknowledged configure in flight.
- **OUT-12** `[M1 · B0]` Click-to-place (`Placement interactive`, default): a
  new toplevel is not shown until placed. Its outline, at the window's initial
  size, hangs from the pointer by its top-left corner; the readout hint reads
  `click to place · B3 to fit`. B1 places it there. B2 press-and-drag sets the
  size from that corner (twm). `Esc` places it where the outline is. The Icon
  Manager lists the window as `placing` meanwhile (IMG-3).
- **OUT-13** `[M3 · B0]` *Decided:* B3 "fit" during placement grows the
  outline to the largest empty rectangle around the pointer that overlaps no
  other frame and stays on the output, and places the window there at once.
  Within the client's maximum size; with the pointer over another window
  there is no room, and B3 places as B1 does.
- **OUT-14** `[M1]` Several windows waiting for placement queue up and are
  placed one at a time, oldest first.
- **OUT-15** `[M1]` *Decided:* transients with a parent skip placement and
  open centered over the parent; XWayland windows with `USPosition` skip
  placement and keep their position. Reason: dialogs should appear where the
  user's attention already is.
- **OUT-16** `[M2 · B7]` With `RememberGeometry on` (default), a window whose
  app id has a remembered geometry maps straight there without placement, and
  the root line says so: `restore  firefox → 1120×900+40+60, from its last session`
  (*decided:* `from earlier` when the geometry was recorded in this session).
  *Decided (M5):* only for the first window of an app: if another window with
  that app id is open, the new one is placed as usual instead of landing on
  top of it.
  Windows without an app id, transients and windows whose remembered output
  is gone are placed as usual.
- **OUT-17** `[owner, after M7]` A window that changes its own size (foot
  keeping its grid when its font changes, an X11 app's resize request)
  gets that size and keeps it: later configures carry the new size, not the
  one twm last asked for. In a tab group it doesn't: tabs share one size
  (GRP-3) and only a resize changes it, so the tab is told the group's size
  again (an app that can't take it, asked twice, wins, and the group follows).

### SNP — outlines that think

- **SNP-1** `[M3 · B3 B8]` During move, resize and place, outline edges snap
  to neighbour frames' outer edges (abutting and aligned), output edges,
  output thirds, and optionally the output centre. `Snap { edges thirds }` is
  the default; `center` adds the centre.
- **SNP-2** `[M3]` Snapping happens within `SnapDistance` (12). Per axis the
  nearest candidate wins; on a tie, frame edges beat thirds.
- **SNP-3** `[M3 · B3]` Each snapped axis shows a guide: a 1 px dashed
  `accent` line across the whole output at the snapped coordinate, with an
  11 px label tracked 1 px in `accent` at its start: `EDGE · ZPOOL` (the
  neighbour's name, uppercase), `⅓ HEIGHT`, `⅔ WIDTH`, `CENTRE`.
  *Decided (M3):* a neighbour's name is the first word of its title (B3:
  "zpool status" → ZPOOL); an output edge reads `OUTPUT EDGE`. Only
  neighbours level with the outline on the other axis (within
  `SnapDistance`) count. Guides lie beneath the outline.
- **SNP-4** `[M3 · B3]` While an outline operation runs with thirds enabled,
  faint thirds guides (1 px dashed `guide-faint`) cross the output.
- **SNP-5** `[M3 · B3]` The readout hint names what snapped:
  `left → zpool · top → ⅓`.
- **SNP-6** `[M3 · B3]` During an outline operation the output shows its name,
  mode and scale bottom-right in 11 px `dim`, 12 px from the corner:
  `DP-1 · 2560×1440 · scale 1.25`.
- **SNP-7** `[M3 · B3]` Holding M moves freely: a press of the modifier after
  the drag has started suspends snapping until it is released. *Decided:*
  this rule also covers drags that began with `M-B1`, whose initial M press
  doesn't count.

### HIS — window operation history

- **HIS-1** `[M3 · B3]` Every place, move, resize, iconify, deiconify,
  squeeze, group, ungroup, send-to-desktop and config write is recorded with
  its target, time, and the state before and after.
- **HIS-2** `[M3 · B3]` `M-z` undoes the newest recorded operation on the
  focused window (or its group); `M-S-z` redoes it. *Decided:* "per window"
  (B3 footer) means undo is scoped to the focused window while the history
  view lists everything. A new operation clears that window's redo.
  *Decided (M3):* placing a window is recorded but can't be undone: there is
  no earlier position to return to. When there's nothing to undo or redo,
  the root line says so.
- **HIS-3** `[M3 · B3]` `f.history` (default `M-S-h`, decided) toggles a
  utility frame `history`: rows of operation (ink-2), target (ink) and age
  (dim, `2m`). An outline operation in progress shows on top as `pending`:
  operation in `accent`, `accent-wash-soft` background, 3 px inset `accent`
  bar on the left. Footer: `M-z undo · M-S-z redo · per window`.
  *Decided (M3):* the newest eight entries show; undone ones are dim with the
  age `undone`; twm's own windows aren't recorded. It opens at the top-right,
  24 px in, until moved (then its position is remembered like any window's;
  the Icon Manager will want that corner in M4). *M4:* it now opens left of
  the Icon Manager's column.
- **HIS-4** `[M3]` History keeps the last 100 operations for the session and is
  not saved.
- **HIS-5** `[M7 · B8]` A write from the Settings window is undoable with
  `M-z` while Settings has focus. *Decided:* `M-z` first takes back pending
  changes, one at a time; with none pending it undoes the last write (the
  old file is written back and applied, or removed if there was none).
  `M-S-z` redoes either. Up to 64 pending steps and 16 writes.

### ROOT — the root

- **ROOT-1** `[M0 · B0]` Each output's root is `ground` with the root weave
  (`RootWeave on`).
- **ROOT-2** `[M1 · B0]` Wordmark `Tom's WM` (`wordmark` role) on the primary
  output: left edge at 51 % of the output width, glyph box bottom at H − 32.
  `RootWordmark off` hides it.
- **ROOT-3** `[M1 · B0]` Status line under the wordmark, `caps` role in `dim`:
  `KESTREL · WAYLAND 0 · 2× OUTPUTS · SLOPPY FOCUS` — host name, the
  `WAYLAND_DISPLAY` name with `-` shown as a space, output count, focus
  policy. Left = wordmark left + 10, top = H − 34. Updates when any of them
  change.
- **ROOT-4** `[M4 · B0]` B1 press on the bare root opens the root menu (MNU-1).
  B2 and B3 on the root are unbound by default.
- **ROOT-5** `[M2 · B7]` The root line: compositor messages (config results,
  geometry restores, later capture notices) each write one line on the root:
  `HH:MM  source   message`, source padded to 8 columns, `root-line` role. At
  most 3 lines show, newest at the bottom, anchored just above the status line
  and aligned with it. Colors: errors `accent`; the newest line `ink-2`; older
  lines `dim`.
- **ROOT-6** `[M2 · B7]` A line disappears `RootLineTimeout` seconds after it
  was written (default 60). The fade lasts at most 80 ms. *Decided (M2):* it
  is removed at once, which is within that limit.
- **ROOT-7** `[M2 · P-4]` Messages never appear as toasts or pop-ups.
- **ROOT-8** `[M4 · B7]` `M-n` toggles a utility frame `log` with the last 200
  root-line messages. *Decided (M4):* it shows the newest 20, oldest at the
  top, and opens at the top-left.
- **ROOT-9** `[M4 · B0]` Root icons: each iconified window gets an icon,
  laid out left to right from (72, H − 140) in 72 px slots 20 px apart. Box
  56 × 56, `surface`, 1 px `hairline`; glyph 28 px in `ink-2` (the app icon in
  one color if available, else the app id's first letter in Plex Mono 600 at
  20 px; *M4: always the letter — app icons need an icon-theme loader, a
  dependency question for later*); label below in 11 px `ink-2` on `ground`
  with 4 px padding. Urgent:
  border, glyph and label in `accent`, label suffix ` · urgent`. *Decided:* a
  count like `(4)` at the start or end of the title becomes the suffix ` · 4`.
- **ROOT-10** `[M4]` B1 on an icon deiconifies and focuses; B2 drags the icon.

### CFG — the config

- **CFG-1** `[M2]` One file: `-f path` if given, else `$TWMRC`, else
  `~/.twmrc`. No file → built-in defaults (exactly the defaults in §5) and a
  root-line note.
- **CFG-2** `[M2 · B0]` Plain text, one setting per line, twm-flavoured:
  `Key value…`, `Key { … }`, `ButtonN = context : function`,
  `"key-spec" : function`, `Menu "name" { "label" function … }`, `!"cmd"` as
  shorthand for `f.exec`, `#` comments, `"…"` strings.
- **CFG-3** `[M2 · P-6]` Every setting has a key (registry: §5). An unknown key
  is an error.
- **CFG-4** `[M2 · B0 B2]` `twm --check [file]` validates without starting a
  compositor. Success prints `config ok · N warnings` and exits 0. Failure
  prints `file:line: message` per problem and exits 1.
- **CFG-5** `[M2 · B0]` Hot reload, triggered by a change to the file
  (directory watch: inotify on Linux, kqueue on FreeBSD, so editor
  rename-saves are seen), `twm --reload`, `M-S-r`, or the menu. Clients keep
  running.
- **CFG-6** `[M2 · B7]` A config with errors is not applied; the old one
  stays, and the root line says, in accent:
  `.twmrc   line 14: unknown key SqueezTitle · previous config kept`.
  A successful reload writes `.twmrc   reloaded · 0 warnings` in normal color.
  When there are several errors the line adds `(+N more)`; `twm --check` and
  `twm --reload` list them all.
- **CFG-7** `[M2]` Reload is atomic: parse and validate completely, then swap.
- **CFG-8** `[M2 · B8]` The parser keeps a lossless syntax tree (comments,
  order, whitespace) so single values can be rewritten in place (SET-7).
- **CFG-9** `[M2 · B0 B8]` Acceptance: the config shown in B0 passes `--check`
  with 0 warnings; every key in the B8 diff is valid; the B7 misspelling
  `SqueezTitle` gives the B7 error at its line.
- **CFG-10** `[M2]` Warnings (deprecated or ignored values) don't block a
  reload.
- **CFG-11** `[M2]` Remembered geometry: per app id, the last committed frame
  geometry and output name, in plain text at
  `$XDG_STATE_HOME/twm/geometry`, written at most once a second.
- **CFG-12** `[M2 · B0]` `twm --reload` reaches the running compositor over a
  UNIX socket (`$XDG_RUNTIME_DIR/twm.$WAYLAND_DISPLAY.sock`) and prints its
  answer, e.g. `config ok · 0 warnings · reloaded in 0.8 ms`.
- **CFG-13** `[M2]` *Decided:* a config with errors at startup leaves the
  built-in defaults running, with the first error on the root line
  (`… · built-in defaults used`). Reason: there is no previous config to keep,
  and a desktop must come up.
- **CFG-14** `[M2]` *Decided:* the built-in defaults are `.twmrc` text
  compiled into twm and read through the same parser. A user file's bindings
  replace the built-in ones for the same key or button, `f.nop` unbinds one,
  and `NoDefaultBindings` drops all built-in bindings (classic twm's
  `NoDefaults`). Reason: B0's sample binds four things; it shouldn't lose the
  other KEY bindings by doing so.
- **CFG-15** `[M2]` *Decided:* a key set twice in one file is a warning (the
  later one wins), as is a binding repeated for the same trigger.

### IMG — Icon Manager

- **IMG-1** `[M4 · B0]` The Icon Manager is a compositor-drawn utility frame
  titled `Icon Manager`, 236 wide, placed by `IconManager { at top-right }`
  with 24 px margins. *Decided:* it stacks like a normal window and shows on
  every desktop.
- **IMG-2** `[M4 · B0 B7]` Desktop row: one button per desktop, 4 per row. The
  current desktop is filled `accent` with `on-accent` 600 text; others have a
  1 px `hairline` outline and `ink-2` text; empty desktops use `dim` text; a
  quiet desktop shows the quiet glyph instead of its number. B1 switches.
- **IMG-3** `[M4 · B0 B5 B7]` Window rows (current desktop, sorted by
  `IconManager { sort name|stack|age }`, default `name`):
  - focused: filled `accent` 8 px square, `accent-wash` row, `ink` text;
  - mapped: 1 px `muted` square outline, `ink-2` text;
  - placing: dashed square outline, right label `placing` in `dim`;
  - urgent: dashed `accent` square, `accent` text, 3 px inset `accent` bar,
    right label `!` (or ROOT-9's count);
  - urgency held by a quiet desktop: right label `quiet` in `dim`;
  - tab group: `name ×N`, right label `tabs` in `dim`.
- **IMG-4** `[M4 · B0]` A divider, then iconified windows (all desktops): 8 × 2
  bar and text in `dim`; urgent ones in `accent` with `!`.
- **IMG-5** `[M4]` B1 on a row raises and focuses the window, deiconifying and
  switching desktop as needed.
- **IMG-6** `[M4 · B5]` Type-to-filter: when the Icon Manager holds keyboard
  focus (sloppy focus applies to it like any frame), typing starts a filter.
  The frame turns focused; a filter row appears (label `filter` 12 px
  `muted`, input `filter-input` on `ground` with a 1 px `hairline-strong`
  border, match count in `dim`). The list narrows in place: nothing zooms,
  dims or moves.
- **IMG-7** `[M4 · B5]` Results come in two sections with 11 px tracked `dim`
  heads: `WINDOWS` (every desktop, iconified included; right label
  `desktop N` or `iconified`), then `RUN` (menu entries, then executables on
  `$PATH`). Matching is case-insensitive substring, prefix matches first; the
  matched part is `accent` 600.
- **IMG-8** `[M4 · B5]` The first result is selected (`accent-wash`, 3 px
  inset `accent` bar); Up/Down move. `Return` raises (switching desktop or
  deiconifying), `M-Return` brings the window to the current desktop,
  `Tab` completes the input to the selected name plus a space, `Esc` clears
  (a second `Esc` leaves). The footer shows these four, keys in `accent`.
- **IMG-9** `[M4 · B5]` *Decided:* a `RUN` menu entry runs its function; a
  `$PATH` executable runs as `Terminal -e <input>` and is labelled
  `in <terminal>` (B5 "in foot"). `Terminal` defaults to `foot`.
- **IMG-10** `[M4 · B5]` The Icon Manager is the only switcher: there is no
  Alt-Tab overlay.
- **IMG-11** `[M4]` *Decided:* while a filter is active the Icon Manager sees
  keys before the key bindings, so `M-Return` is "bring here" there (B5) and
  the terminal everywhere else (KEY). With an empty filter the bindings come
  first and only plain typing starts a filter. Leaving the Icon Manager (its
  focus moves elsewhere) clears the filter.
- **IMG-12** `[M4]` *Decided:* `RUN` menu entries carry the meta `menu
  <name>`; executables show as `name …` (B5) and list by prefix. A `%s` in
  `Terminal` stands for the command (`"xterm -e %s"`); without one the
  command is appended (`"foot"` → `foot htop`).
- **IMG-13** `[owner, after M7]` `RUN` offers the installed applications too:
  the `.desktop` entries under `$XDG_DATA_HOME` and `$XDG_DATA_DIRS`
  (`applications/`, the first of an id winning), between the menu entries and
  the executables, by Name, prefix matches first; the meta is the entry's
  GenericName (else `app`). They run as their Exec says, field codes left
  out, and in the Terminal only with `Terminal=true`. Left out: NoDisplay,
  Hidden, a missing TryExec, OnlyShowIn without `twm` or NotShowIn with it.
  Read again after a minute. `f.run` (`M-d`, the menu's `run…`) brings up
  the Icon Manager, focused, ready to type into.

### MNU — root menu

- **MNU-1** `[M4 · B0]` B1 on the root opens menu `main` with its top-left at
  the pointer, kept on the output. Release over an item runs it
  (press-drag-release); a click opens the menu to stay, a second click runs.
  `Esc` or a click outside closes.
- **MNU-2** `[M4 · B0]` Drawing: 260 wide, `surface`, 1 px `hairline-strong`
  border. Header 32 px `graphite` with `TOM'S WM` (`menu-header`) left and a
  60 × 10 hatch right. Items 30 px, 14 padding: label `menu` left, shortcut
  `dim` right. Separators 1 px `hairline` with 4/14 margins. The item under the
  pointer or keyboard is `accent` with `on-accent` 600 label and shortcut.
  Submenus show `▸`.
- **MNU-3** `[M4 · B0]` The shortcut column is generated from the bindings
  for the same function; menus never hard-code shortcut text. *Decided (M4):*
  a plain title-bar button binding wins over a key (B0 shows `B2` for move,
  `B3` for squeeze), and `Return`/`space` read `↵`/`␣`.
- **MNU-4** `[M4 · B0]` Default `main` menu: xterm · browser · files |
  move · resize · squeeze · iconify · raise / lower | desktops ▸ ·
  reload .twmrc · exit. *M7:* `settings` after `desktops ▸` (SET-10).
  *After M7:* `run…` after `files` (IMG-13), `close` and `kill` after
  `raise / lower` (FRM-19).
- **MNU-5** `[M4]` Window functions run from a menu ask for a target: the
  cursor becomes a crosshair, the next B1 on a window applies, `Esc` cancels
  (twm). *Decided (M4):* a move or resize started this way (or from the
  keyboard) has its outline follow the pointer and ends on the next click.
- **MNU-6** `[M4 · P-8]` Arrow keys, `Return` and `Esc` drive the menu;
  `M-m` (decided) opens `main` at the pointer.
- **MNU-7** `[M4]` Menus are defined in `.twmrc` with `Menu "name" { … }`;
  `f.menu "name"` opens one, including as a submenu.

### DSK — desktops and Panner

- **DSK-1** `[M4 · B0]` N virtual desktops (default 4, `Panner { desktops 4 }`
  as in B0). *Decided:* `M-1`…`M-9` switch, `M-S-1`…`M-S-9` send the focused
  window.
- **DSK-2** `[M4 · P-3]` Switching is instant.
- **DSK-3** `[M4 · B0]` Panner: utility frame `Panner`, bottom-right, 24 px
  margins, 236 wide; a 2-column grid of desktop miniatures (cell height 62,
  gap 6, padding 10). Current desktop cell `accent` border, others `hairline`,
  empty ones dashed `hairline`. Window miniatures are 1 px `mini` outlines;
  the focused window's is `accent` with `accent-wash-mini` fill.
- **DSK-4** `[M4]` *Decided:* B1 on a cell switches desktop; B2 dragging a
  miniature to another cell sends that window (vtwm/ctwm).
- **DSK-5** `[M4]` The Icon Manager's desktop row and the Panner always agree.
- **DSK-6** `[M4]` *Decided:* new windows open on the current desktop
  (transients on their parent's); twm's own windows are on every desktop. On
  a switch, focus goes to the window most recently focused there; when the
  focused window leaves the screen (sent, iconified) the same rule applies,
  but nothing that had no focus gains it (FOC-9). Sending is recorded in the
  history and undoable.

### GRP — tab groups

- **GRP-1** `[M5 · B2]` B2-dragging a window's tab and releasing it on another
  frame's tab row or top edge joins the two in one frame. *Decided (M5):* the
  drop zone is the target's title band across its whole width; the dropped
  tab becomes the active one and takes focus; a group joins others one tab at
  a time (dragging a whole group onto another doesn't merge them).
- **GRP-2** `[M5 · B2]` Over a valid target the outline gives way to a
  floating tab (`graphite`, `ink` text, 1 px inset `accent`) under the
  pointer; the target row opens a dashed `accent` slot the width of the
  dragged tab at the insertion point (4 px margins), and `DROP → GROUP OF N`
  (11 px, tracked 1, `accent`) shows near the pointer.
- **GRP-3** `[M5 · B2]` A group frame has one border and one client area (all
  members get the same size; a member can't change it on its own, OUT-17)
  and one tab row: iconify box, the tabs, resize
  box. Each tab stays squeezed to its name. The active tab is focused-style
  with its hatch inside, 12 after the title; inactive tabs are `graphite`
  with `muted` 500 text and a 1 px `hairline` right separator. Each button
  takes the colors of the tab beside it.
- **GRP-4** `[M5 · B2]` B1 on an inactive tab activates it. `M-Tab` cycles
  the active tab within the focused group only.
- **GRP-5** `[M5 · B2]` B2-dragging a tab off the row splits it out; it is
  placed by outline like a new window (OUT-12). *Decided (M5):* in a group,
  B2 on a tab drags that tab (the outline hangs from the pointer once it
  leaves the row; released, it stands there alone; released on its own row,
  nothing happens); the whole group moves with B1-drag, `M-B1` or `M-S-m`.
- **GRP-6** `[M5 · B2]` B3 on a group's tab row folds every tab into the
  active one; B3 again fans them out. (On a group, B3 folds instead of
  squeezing; FRM-14.) *After M7, at the owner's request:* folding moved to
  M-B3 (`f.fold`); B3 squeezes, GRP-9.
- **GRP-9** `[owner, after M7]` Squeeze (B3 on the row, `f.squeeze`) acts on
  the whole group, as on one window (FRM-14): the body goes, the row with
  every tab stays; another tab brought to the front is squeezed too, and B3
  again opens the group. Undo takes it back (HIS-1).
- **GRP-7** `[M5]` Move, resize and iconify act on the whole group. The Icon
  Manager shows one row (IMG-3).
- **GRP-8** `[M5]` *Decided:* joining and leaving are recorded (HIS-1);
  undoing a detach puts the tab back, undoing a join takes it out again.
  The filter (IMG-7) still finds each tab; choosing one brings its group up
  with that tab in front. A group's tab row is cut to fit like a single tab
  (FRM-5): the longest titles give way first.

### ZOM — zoom and fullscreen

- **ZOM-1** `[owner, after M7]` twm's zooms, framed: `f.zoom` makes the
  frame as tall as the output's usable area (exclusive zones left out,
  GEN-2), keeping its x and width; `f.horizoom` as wide, keeping its y and
  height; `f.fullzoom` both. The frame stays: title tab, border, buttons.
  The same function again puts the window back where it was before any
  zoom; another zoom function switches straight to that zoom.
- **ZOM-2** `[owner, after M7]` *Decided:* the window remembers one
  "normal" geometry while zoomed or fullscreen, and zooms are worked out
  from it and the output each time, so `f.zoom` after `f.horizoom` gives a
  tall window at its normal x, not a full one. Moving or resizing a zoomed
  window by hand (outline, keys) ends the zoom where it lands. A squeezed
  window opens before it zooms; an iconified one doesn't zoom. Remembered
  geometry (OUT-16) stores the normal geometry, never a zoomed one.
- **ZOM-3** `[owner, after M7]` `f.fullscreen` shows the window's client
  alone over its whole output: no tab, no border, the output's box rather
  than its usable area, above layer-shell bars. Again, it returns the
  window to what it was (normal or zoomed). A client smaller than the
  output (one that won't take the size) is centred on black.
- **ZOM-4** `[owner, after M7]` *Decided:* a fullscreen window is above the
  bars only while it has the focus. When the focus goes to another window
  (sloppy focus on another output, `M-u`, the Icon Manager), it steps back
  among the windows, just under the one that took the focus, still
  frameless at the output's size, and comes forward again when focused.
  (The Icon Manager is a window like the others: it may stay under it.) Nothing is hidden from the user by a window
  they aren't using.
- **ZOM-5** `[owner, after M7]` App requests: an xdg-shell maximize is
  `f.fullzoom` and unmaximize puts it back; an X11 window asking for only
  vertical or only horizontal maximize gets `f.zoom` or `f.horizoom`. A
  fullscreen request (F11, a video's button) is `f.fullscreen`, on the
  output it names if any. A window that maps already maximized or
  fullscreen skips click-to-place and opens that way on the output under
  the pointer. Clients are told the truth back: maximized for
  `f.fullzoom`, tiled edges for the other zooms (xdg-shell v2+), fullscreen
  for fullscreen.
- **ZOM-6** `[owner, after M7]` *Decided — the SCREEN marker in fullscreen
  (CAP-2):* a fullscreen window that captures the screen has no tab to
  carry its marker, so twm draws one over it: a 2 px `alert` line along
  the output's top edge, and the tab's marker itself (dot and `SCREEN`, as
  in CAP-1, `graphite` ground) at the top-right corner, title-band high.
  Both stay while it captures, and the pointer passes through them to the
  window. Like every marker, it can't be turned off.
- **ZOM-7** `[owner, after M7]` *Decided:* zooms and fullscreen are recorded
  (HIS-1) — `zoom` and `fullscreen` in the history — and `M-z` takes them
  back. A group zooms and goes fullscreen as one window (GRP-7): the state
  belongs to the tab in front and passes to the next tab brought forward.
  A fullscreen window can't be moved or resized by outline; leave
  fullscreen first.
- **ZOM-8** `[owner, after M7]` *Decided — trust:* fullscreen is the one
  place a client draws without twm's frame around it, so it could paint
  something that looks like a trusted prompt. Real prompts (TRU-1) take the
  focus, so a fullscreen window steps back behind the dim (ZOM-4), and a
  trusted prompt never appears with a fullscreen window in front of it.
- **ZOM-9** `[owner, after M7]` Default keys: `M-Up` → `f.fullzoom`,
  `M-S-f` → `f.fullscreen`. The root menu gets "zoom" and "fullscreen".
  `f.zoom` and `f.horizoom` have no default key (twm had none either).

### FET — twmfetch

- **FET-1** `[owner, after M7 · B9]` `twmfetch` prints a logo beside a
  column of facts, in the running theme's colors (24-bit escapes when the
  output is a terminal; none with `NO_COLOR` or into a pipe). The facts, in
  B9's order, labels in `accent`: `os`, `uptime`, `wm`, `frames`,
  `desktops`, `outputs`, `focus`, `font`, `pool`, `memory`, `.twmrc`; then
  a blank line and a strip of eight swatches from the theme's tokens
  (`ground` with a `hairline` edge, `surface`, `graphite`, `muted`, `ink`,
  `accent`, `ok`, `alert`). `user@host` heads the column, `host` bold,
  `user` in `accent`, a `rule`-coloured underline. `ONLINE` and `check ok`
  are in `ok`.
- **FET-2** `[owner, after M7 · B9]` *Decided:* the logo is B9's, not the
  design document's matrix: a squeezed tab (` ■ tom's wm ░░░░ `, `accent`
  ground) over a box-drawn frame holding `twm` in box-drawing letters and
  `since 1987` in `muted`. A terminal has no pixels for the mark (MRK).
  `--logo off` prints the facts alone.
- **FET-3** `[owner, after M7]` *Decided:* the `wm`, `frames`, `desktops`,
  `outputs`, `focus`, `font` and `.twmrc` facts come from twm over its
  socket: a read-only `info` request answering counts, output names, modes
  and scales, and the config's state — never a window's title or app id.
  Outside twm those rows say `not running`. `os`, `uptime` and `memory` come
  from the kernel; `pool` from `zpool list` when ZFS is there, else the row
  is left out.
- **FET-4** `[owner, after M7 · B9]` `--json` prints every fact as one JSON
  object (for scripts); `--short` one line for a prompt
  (`tom@kestrel · twm 0.1 · 7 frames · desk 1/4 · up 12d 4h`); `--no-shot`
  writes nothing.
- **FET-5** `[owner, after M7]` A plain `twmfetch` under twm also asks twm
  for a fetch shot (SHOT-2) and prints its path last, in `muted`.
  `twmfetch <image>` applies a shared look (SHR).

### CARD — the root card

- **CARD-1** `[owner, after M7 · B9]` `RootCard { at bottom-left fields host
  uptime load pool mem frames }` draws the same facts on the root, like the
  wordmark and the root line: no frame, never in the Icon Manager, under
  every window. *Decided:* off by default (P-4 keeps the bare desktop calm);
  `at` takes the four corners; a field it doesn't know is a config error.
- **CARD-2** `[owner, after M7 · B9]` From B9: the host name in the
  wordmark's serif italic, 44, `ink` at 55 %; then 13 px lines:
  `up 12d 4h  ·  load ▂▃▅▃▂▁▂▃ 0.42` (eight samples, `accent`),
  `tank ▮▮▮▮▮▮▮▮▮▮  4.1T free` and `mem ▮▮▮▮▮▮▮▮▮▮  9.8 / 32G` (ten cells, the
  used share in `accent`, the rest `#3A3D34`, labels `muted`), and
  `7 frames · 2 groups · desk 1/4` in `muted`. Its corner is 80 px in from
  the output's usable edges, like the greeter's (B11).
- **CARD-3** `[owner, after M7]` It updates once a second; the pool, which
  costs a process, every 30 s, in the background. B1 on it opens
  `twmfetch` in the `Terminal`, kept open afterwards.

### SHOT — screenshots

- **SHOT-1** `[owner, after M7]` `Print` (`f.screenshot`) saves the output
  under the pointer, as it is, to `$XDG_PICTURES_DIR/Screenshots/
  twm-YYYYMMDD-HHMMSS.png` (else `~/Pictures/Screenshots`), and a root line
  says where. The compositor takes it (no client captures), so no capture
  marker; the root line is the record (CAP-3).
- **SHOT-2** `[owner, after M7 · B9]` `M-Print` (`f.fetchshot`) is the fetch
  shot: the same capture with the root card drawn in for that frame (where
  `RootCard` puts it, bottom-left if it's off), written to
  `/tmp/twmfetch-YYYYMMDD-HHMMSS.png` (`$TMPDIR` if set) with the `.twmrc` in a PNG text chunk
  (`iTXt`, UTF-8: `twm:twmrc`, and `twm:version`). *Decided — the design document:* `/tmp`
  is the point: kept only if moved out.
- **SHOT-3** `[owner, after M7]` *Decided:* a shot is the whole output as
  you see it — it is your own key press. While a trusted prompt or the lock
  is up, no shot is taken (TRU-4, GRT-7); the root line says so.
- **SHOT-4** *Decided:* the PNG is compressed by twm's own small deflate
  (D-11): no zlib.

### SHR — sharing a look

- **SHR-1** `[owner, after M7]` `twmfetch <image>`, or a PNG dropped on the
  bare root, reads the image's `twm:twmrc` chunk. None: a root line says
  the image carries no look.
- **SHR-2** `[owner, after M7]` *Decided — appearance only:* a `.twmrc`
  also holds bindings and menus that run commands, so taking it whole would
  make a screenshot executable. Only appearance keys are taken — `Theme`,
  `Accent`, `Font`, `TitleHeight`, `BorderWidth`, `TitleHighlight`,
  `SqueezeTitle`, `CloseBox`, `RootWeave`, `RootWordmark`, `RootCard`,
  `IconManager`, `IconsOnRoot`, `Effects` — and every other key, binding and
  menu stays the recipient's own.
- **SHR-3** `[owner, after M7]` Reconcile, check, diff, apply: the taken
  keys are merged into the recipient's file (comments and order kept,
  SET-7), the result must pass `twm --check`, and the Settings window opens
  with them as pending edits, so the diff is in view (SET-4); Write applies
  it and `M-z` undoes it (HIS-1). A root line says what was taken, what
  stayed and why (`look from twmfetch-….png · 6 keys taken · 41 lines
  kept`).
- **SHR-4** `[owner, after M7]` *Not yet:* dropping on a Panner cell for one
  desktop's look (twm has no per-desktop settings); photo wallpapers (twm
  has the weave, and layer-shell for the rest).

### MRK — the mark

- **MRK-1** `[owner, after M7 · B10]` The project's mark: `twm` in IBM Plex
  Mono 700 as a mask over a field of modules (four shades, three sizes), a
  sparse scatter shed past the glyphs; coarser as it shrinks, solid
  `accent` at favicon size. For the README and the session's icon.
- **MRK-2** `[owner, after M7 · B13]` *Not yet — the design document's own
  open question:* the mark as a carrier in the pixels, for paths that strip
  metadata. B13's scheme (3 bits a module, read row by row) has no error
  correction or framing; a spike decides the real encoding and its byte
  budget before any shot carries it. Until then the chunk is the only
  carrier. *Spike done (docs/spikes/mark.md):* the size channel doesn't
  survive downscale and JPEG; shade alone (2 bits a module) does at 10–12
  px cells; a 500 px mark holds ~47–73 raw bytes, so the mark should carry
  the look's delta from the defaults with Reed–Solomon parity, not text.

### GRT — the greeter and the lock

- **GRT-1** `[owner, after M7 · B11 B12]` One surface, two modes:
  `twm --greeter`, the greeter greetd runs at boot (twm as the compositor,
  the greeter its only window), and the lock over a running session. Same
  chrome, the same trusted frame (TRU-1 … TRU-3): the weave is twm's, so a
  prompt that wears it is the real one.
- **GRT-2** `[owner, after M7 · B11]` The screen, 1:1 from B11: the root
  dimmed one step (TRU-4); the frame 560 wide, centred, its tab
  `system · sign in` or `system · locked` with the lock glyph; an 11 px
  weave band; inside on `surface`: the user's name in serif italic 44 with
  `@ host` in `muted`, `PASSWORD` (12, tracked 1), a 46 px field with a `%`
  and an `accent` caret block, and `caps off · us · ⏎ unlock` under it. A
  `graphite` badge above the frame: the lock glyph and `LOCKED · IDLE 9 MIN`
  (`accent`, tracked 2). The time top-right (40 px) over the date (12,
  `muted`). *Decided:* B11's round clock face is left out — the capture dot
  stays the only round shape (CAP-4). The root line runs along the bottom
  as usual.
- **GRT-3** `[owner, after M7 · B12]` Login: a USER list (selected: an
  `accent` square, a 10 % `accent` row and a 3 px `accent` left edge;
  others hollow squares), the password field, and SESSION with a button
  that opens a session menu drawn as a root menu (name, then `wayland` or
  `X11` in `muted`). Users are the accounts with a uid in the normal range
  and a login shell. Sessions are `wayland-sessions` and `xsessions`
  entries. The last session is remembered per user in the greeter's own
  state.
- **GRT-4** `[owner, after M7 · B12]` Lock: the session's user only, no
  session list, `session kept · N frames held` under the field.
- **GRT-5** `[owner, after M7 · B12]` A failed try: the frame goes dashed,
  the tab `graphite` with an `accent` outline, the error prints like a root
  line in `alert` (`pam: authentication failed · try 2 of 5`), the field
  clears. Nothing else moves. *Decided:* after five, a minute's wait, said
  on the same line.
- **GRT-6** `[owner, after M7]` PAM underneath (D-12): the prompt is the PAM
  conversation, shown as it comes — a hidden prompt is the password field,
  a visible one a text field, info and errors are lines. PAM runs in a
  child process, so twm never waits on it. The `twm` PAM service is
  installed with the distribution's own auth stack (Linux `common-auth` or
  `system-auth`, FreeBSD `system`). At boot greetd does the PAM part and
  the greeter relays its conversation.
- **GRT-7** `[owner, after M7]` Locking: `f.lock` (`M-l`), `twm --lock`,
  after `IdleLock` seconds without input (default 600, `0` never), and
  before sleep (`LockOnSleep on`, through logind on Linux). While locked
  every window is hidden, not dimmed (B12: "frames held"); input goes to the
  lock alone and synthetic input nowhere (TRU-4); urgency is held until
  unlock (URG-3) and the root line says how many; no shots (SHOT-3).
  `IdleScreen` seconds (default 900) turns the outputs off until the next
  input. A client's idle inhibitor (a video playing) holds both off.
- **GRT-9** `[owner, after M7]` *Decided — tests:* with `--test`, an
  environment variable (`TWM_TEST_LOCK_PASSWORD`) stands in for PAM, so
  the screenshot test can unlock; the startup command is trusted already
  (CAP-6), and without `--test` the variable is ignored. `f.suspend`
  (root menu "suspend") locks first when `LockOnSleep` is on, then asks
  logind (or `zzz`). The menu also gets "lock". `twm --lock` locks the
  running session. Ctrl-Alt-Fn switches virtual terminal, locked or not:
  the lock is twm's state, so it holds on every VT.
- **GRT-10** `[owner, after M7]` *Decided — the greeter's keys and
  choices:* ↑↓ choose the user (a new greetd conversation each time), Tab
  and Shift-Tab the session, Return answers, Esc clears; B1 on a user row
  or the session button does the same. With no account to list, the name
  is typed first. Only `wayland-sessions` are listed: greetd starts an X11
  session only through a wrapper, and twm has no X11 mode. The session's
  `Exec` goes to greetd as words, with `XDG_SESSION_TYPE`,
  `XDG_SESSION_DESKTOP` and `XDG_CURRENT_DESKTOP` (from `DesktopNames`).
  The default session is the user's last, else Tom's WM, else the first.
  The card says how many accounts and sessions there are; there is no
  badge and the bottom line shows the keys. The greeter watches no file
  and serves no portal. `--test` takes `TWM_TEST_GREETER_USERS` and does
  not exit once the session starts, so the test can read its results.
- **GRT-11** `[owner, after M7]` The fingerprint first, tried again and
  again, and the password any time. The lock runs two PAM conversations
  side by side: `twm-fingerprint` (the reader alone, a 10 s try) starts
  over after each timeout or mismatch, up to 20 tries, and `twm-password`
  (the password alone) starts when Return is pressed with something typed;
  either one unlocks. A reader that fails without a word (none there, no
  finger enrolled) is left alone. After 20 tries it rests; Return on an
  empty field starts it again. The greeter has greetd's one conversation
  (the system stack, reader first): when the reader gives up and PAM asks
  for the password with nothing typed, it begins again, up to 20 times;
  what is typed meanwhile goes the moment PAM asks for it. The field takes
  typing all the while, and the reader's words show under it.
- **GRT-8** `[owner, after M7]` *Decided:* the boot greeter replaces
  Draftpaper's on the owner's machine; greetd's previous config is kept
  beside it, so going back is one line. The greeter runs without XWayland
  and without any user's `.twmrc`, in the default theme; it exits once
  greetd starts the session.

### HNT — hint mode

- **HNT-1** `[M5 · B4]` `M-;` enters hint mode: every visible tab on the
  current desktop shows a hint letter where its iconify box was (same box,
  `accent` fill, `hint-letter`). Nothing else changes and nothing moves.
- **HNT-2** `[M5 · B4]` Letters come from `HintKeys` (default `asdfjklgh`,
  home row first). *Decided:* "nearest first" means distance from the
  pointer to the frame; ties go to the higher window in the stack. More
  windows than letters → two-letter codes. *Decided (M5):* twm's own windows
  (title-only tabs, FRM-18) and `NoTitle` windows get no hint; a group gets
  one.
- **HNT-3** `[M5 · B4]` Typing a window's letter marks it selected for one
  frame (letter box `ground` with `accent` letter and a 2 px inset `accent`
  border; tab 1 px inset `accent`, `ink` text; frame `accent`), then the
  window raises and takes focus and hint mode ends. *Decided (M5):* "one
  frame" is 60 ms (within P-3's 80 ms); with two-letter codes the first
  letter narrows the hints to those starting with it.
- **HNT-4** `[M5 · B4]` `Esc`, or any key that is not a hint, leaves hint mode
  with nothing changed.
- **HNT-5** `[M5]` Hint mode holds the keyboard; no client gets keys until it
  ends. *Decided (M5):* modifier keys alone don't end it; a click ends it and
  then does what it would have done.

### URG — polite urgency

- **URG-1** `[M6 · B7]` A window never takes focus because it asked. An
  xdg-activation request becomes urgency unless its token was issued for a
  user input to the currently focused client (e.g. clicking a link). XWayland
  urgency hints and `_NET_ACTIVE_WINDOW` requests become urgency. *Decided:*
  honouring tokens tied to the user's own click is following, not stealing.
- **URG-2** `[M6 · B0 B1 B7]` Urgency shows on the frame (FRM-9), the Icon
  Manager (IMG-3, IMG-4) and the root icon (ROOT-9). `[M1]` The frame state is
  built in M1 so the anatomy test covers it; in M1 any xdg-activation request
  without a token marks the window urgent.
- **URG-3** `[M6 · B7]` Quiet desktops (`Quiet { 3 }`, or B3 on the desktop's
  button): urgency on their windows is held — `quiet` in the Icon Manager, no
  accent — until the desktop is visited.
- **URG-4** `[M6 · B7]` `M-u` jumps to the oldest urgent window (switching
  desktop and deiconifying as needed); `M-u` again, before focus moves
  elsewhere, goes back.
- **URG-5** `[M6]` Focusing a window clears its urgency.

### CAP — capture markers

- **CAP-1** `[M6 · B6]` A window whose client is capturing the screen (wlr
  screencopy, ext-image-copy-capture, portal ScreenCast), the microphone or
  the camera (when brokered by the portal backend) carries a marker in its
  tab after the title: 1 px `hairline` left separator, 12 padding, a 7 px
  round dot and `SCREEN · MIC` (11 px, tracked 1, 600, `alert`). The same
  marker shows on its Icon Manager row, until capture stops.
- **CAP-2** `[M6]` Markers cannot be turned off — a deliberate exception to
  CFG-3.
- **CAP-3** `[M6]` *Decided:* a capturing client without a window (e.g.
  `grim`) gets a root line that stays while it captures:
  `capture  grim is recording DP-1`.
- **CAP-4** `[M6]` *Decided:* the 7 px dot is the only round shape in the UI;
  it is a dot, not a rounded corner, and is drawn as on B6. So the trusted
  frame's choice rows use square markers, not round radio buttons.
- **CAP-5** `[M6]` *Decided:* what twm can see, it marks: wlr-screencopy
  frames and ext-image-copy-capture sessions, per client; a client counts as
  capturing until 2 s after its last frame. A tab inside a group carries the
  marker in the group's row (the title gives way, the marker keeps its
  width), and the group's Icon Manager row says `SCREEN` when any tab
  captures. *Not yet:* portal ScreenCast is captured by
  xdg-desktop-portal-wlr, so twm sees xdpw capturing (a CAP-3 root line), not
  the app that asked; marking the app itself, and `MIC`/camera, needs twm's
  own ScreenCast backend, which means PipeWire — a dependency question for
  the owner (§7).
- **CAP-6** `[M6]` *Decided:* `--test` only: the startup command (the test
  program) is trusted, its own screenshots go unmarked. Every other client,
  including the test's child processes, is marked as usual.

### TRU — the trusted frame

- **TRU-1** `[M6 · B6]` Only compositor-owned prompts (screen-share picker and
  other portal consent dialogs, password prompts) wear the trusted frame: an
  `accent` border, a 10 px trusted-weave band, then a 1 px `hairline-strong`
  border around the `surface` content.
- **TRU-2** `[M6 · B6]` Its tab: `accent`, the lock glyph in the iconify slot,
  title `system · <purpose>`, hatch, no resize box.
- **TRU-3** `[M6 · B6]` No client surface can ever get the weave band or the
  lock. Trusted frames stack above all client windows and the band is drawn
  above any client content.
- **TRU-4** `[M6 · B6]` While a trusted frame is open, keyboard and pointer
  input reach only it; synthetic input (virtual keyboard or pointer protocols)
  never reaches it at all. The rest of the desktop dims by one step —
  *decided:* a `ground` overlay at 35 % — and never more.
- **TRU-5** `[M6 · B6]` Prompt controls: choice rows 35 high with a 1 px
  `hairline` border, the selected one `accent` bordered on `accent-wash-soft`;
  secondary button outlined `hairline-strong`, primary filled `accent` with
  `on-accent` 600 text; buttons 38 high.
- **TRU-6** `[M6 · B6]` Strings that come from a client (app name, origin) are
  drawn verbatim in `ink-2`, never in the prompt's heading style.
- **TRU-7** `[M6]` *Decided — how prompts behave:*
  - The heading is always twm's own words, fixed per kind of prompt:
    `Choose what to share` (screen-share chooser), `An app asks for
    permission` (portal Access). The asker's words follow on one or two
    `ink-2` lines, newlines flattened. Button labels are twm's too
    (`Deny`/`Share`, `Deny`/`Allow`): the portal's `grant_label` and
    `deny_label` are ignored, or an app could label Allow "Deny". Choice rows
    show the options as given (they are what is being chosen).
  - One prompt at a time, in arrival order; the others wait. Opening one
    cancels any move, resize, menu, hint mode or click-to-select; windows
    waiting for click-to-place wait until it closes. New windows don't get
    the focus while it is open. On close, focus returns to the window that
    had it.
  - Centred on the output under the pointer; it can't be moved, iconified
    or resized. Up/Down choose, Space ticks the checkbox, Return grants, Esc
    denies; B1 on a row, the checkbox or a button.
  - Synthetic input (TRU-4) is dropped while a prompt is up — pointer
    motion too, so a remote tool can't even move the cursor. *Test mode:*
    with `--test`, the startup command's own virtual keyboard and pointer are
    trusted so the headless tests can answer prompts; any other client's are
    not. Outside a prompt, virtual input works as before (D-6).
- **TRU-8** `[M6]` *Decided — where prompts come from:*
  - `twm --choose [who]`: a dmenu-style chooser for xdg-desktop-portal-wlr
    (`chooser_type=dmenu`, `chooser_cmd=twm --choose`). Lines from stdin
    become rows; the chosen line, verbatim, goes to stdout; Deny or Esc
    prints nothing and exits 1. It talks to twm over the IPC socket (D-8).
  - The portal backend `org.freedesktop.impl.portal.desktop.twm` serves
    `org.freedesktop.impl.portal.Access`: the first choice with options
    becomes rows (its label the legend), the first boolean choice the
    checkbox; `Request.Close` withdraws the prompt.
  - *Not yet:* polkit password prompts. A polkit agent needs
    libpolkit-agent (or reimplementing its helper protocol), a dependency
    question for the owner (§7). The trusted frame is ready for it.

### INP — touchpad and touchscreen

- **INP-1** `[owner, after M7]` The touchpad's settings are `.twmrc` keys,
  applied through libinput to every touchpad (a device with tap fingers) at
  once and again on each reload: `TouchpadTap`, `TouchpadTapDrag`,
  `TouchpadNaturalScroll`, `TouchpadDisableWhileTyping`, `TouchpadScroll`
  (two-finger/edge/none), `TouchpadClick` (default/areas/clickfinger),
  `TouchpadSpeed` (−1 … 1), `TouchpadAccel` (adaptive/flat). Other pointers
  (a mouse, a TrackPoint) keep libinput's own defaults. *Decided:* the
  defaults are tap and tap-and-drag on, natural scrolling off, the touchpad
  resting while typing, two-finger scrolling, the device's click method,
  speed 0, adaptive — libinput's, except tap, which most laptops want and
  libinput leaves off.
- **INP-2** `[owner, after M7]` A setting a device doesn't have (natural
  scrolling on a pad without it, an unsupported scroll or click method) is
  left alone on that device, not an error. The log names each touchpad and
  what it was given.
- **INP-3** `[owner, after M7]` A touchscreen is mapped to its own output:
  the one udev names for it (`WL_OUTPUT`), else the built-in panel (eDP,
  LVDS, DSI), else the only output; again whenever outputs change.
  `Touchscreen off` turns it off (libinput stops sending its events).
- **INP-4** `[owner, after M7]` A finger on an app that takes touch is
  touch, for that app, in its own coordinates; the touch focuses (and with
  `RaiseOnClick`, raises) the window, as a press does (FOC-5). The seat
  offers touch only while a touchscreen is present.
- **INP-5** `[owner, after M7]` Everywhere else — twm's tabs, buttons and
  windows, the root, the Icon Manager, an app that takes no touch — the
  first finger is the pointer: the pointer goes there, touching is a B1
  press and lifting the release, so a tap clicks and a drag moves. While a
  grab, a menu, click-to-select, hint mode or a trusted prompt is up, every
  finger is the pointer (a prompt still takes it, TRU-4: a touchscreen is a
  real device). Only one finger at a time is the pointer.
- **INP-6** `[owner, after M7]` *Decided — tests:* with `--test`, the IPC
  socket takes `touch down|move|up …`, which twm turns into events of a
  touchscreen of its own, through the same path a real one's take.

### LVL — volume and brightness

- **LVL-1** `[owner, after M7]` The media keys work: `XF86AudioRaiseVolume`,
  `XF86AudioLowerVolume`, `XF86AudioMute`, `XF86AudioMicMute` run
  `f.volume up|down|mute|mic`; `XF86MonBrightnessUp/Down` run
  `f.brightness up|down`. A press moves `VolumeStep` or `BrightnessStep`
  percent (default 5); volume stops at 100 %, brightness above black.
- **LVL-2** `[owner, after M7]` *Decided:* twm adds no audio or backlight
  library; it runs the system's own tool, the first it finds: PipeWire's
  `wpctl`, PulseAudio's `pactl`, ALSA's `amixer`, FreeBSD's `mixer`;
  `brightnessctl`, a writable `/sys/class/backlight`, FreeBSD's `backlight`.
  The line runs in the background and twm reads the new level back from it;
  it never waits on it.
- **LVL-3** `[owner, after M7]` With no tool for it, the root line says what
  is missing, in `accent`.
- **LVL-4** `[owner, after M7]` The new level is one root line —
  `volume  55%  ■■■■■■■■■■■·········`, `mic  muted`, `brightness …` — that
  changes in place while the key is pressed again and goes 3 s after the
  last press (ROOT-7: no pop-up, no on-screen display).

### SET — settings, as a file

- **SET-1** `[M7 · B8]` A Settings window, `settings — ~/.twmrc`, edits the
  file. It is not a second source of truth.
- **SET-2** `[M7 · B8]` Sections: Look, Frames, Focus, Keys & buttons,
  Desktops, Outputs, Privacy, Menus; a `filter keys…` field narrows the
  controls. The nav footer reads `Everything here is also man 5 twmrc.`
  *After M7, at the owner's request:* a ninth section, "Touchpad & touch",
  after Focus (INP-1, INP-3).
- **SET-3** `[M7 · B8]` Each control row shows its label, the control, and the
  `.twmrc` key it writes (12 px `dim`, right-aligned); a changed key turns
  `accent` with ` ●`.
- **SET-4** `[M7 · B8]` A live preview shows a focused and an unfocused tab
  with the pending values.
- **SET-5** `[M7 · B8]` The diff pane is always visible: header `~/.twmrc` and
  `N changes` in `accent`; unified diff lines with line numbers (removed
  `alert` on `alert-wash`, added `ok` on `ok-wash`, context `dim`); the
  `twm --check` result; `Discard` and `Write .twmrc` (primary).
- **SET-6** `[M7 · B8]` Saving is applying: the file is written atomically and
  hot reload (CFG-5) applies it.
- **SET-7** `[M7 · B8]` Comments and the user's ordering are kept; edits
  change only the affected values; new keys are appended.
- **SET-8** `[M7 · B8]` A hand edit of the file updates the controls.
- **SET-9** `[M7 · B8]` B8's controls: `SqueezeTitle` segmented
  (left/center/right/off), `TitleHeight` slider 22–34, `TitleHighlight`
  checkbox, `OpaqueMove` outline/opaque, `Snap` checkboxes
  (edges/thirds/centre), `Accent` swatches plus hex.
- **SET-10** `[M7]` *Decided — the window:* compositor-drawn, like the Icon
  Manager (closes the §7 question; no new dependency). `f.settings`,
  default `M-,`, and a `settings` entry in the root menu (after
  `desktops ▸`, a small addition to MNU-4). B8's sizes at 26/30: body
  1135 × 527 at least (it is resizable, as B8's resize box says; more room
  shows more diff), nav 165, diff pane 364, rows 49, controls 29, buttons
  38; text 12 for labels and nav, 11 for controls, keys and the diff
  (SET-3's "12 px" is B8's CSS size, scaled like the rest), 10 for notes.
  It opens on Frames, centred. The slider thumb is a square (CAP-4).
- **SET-11** `[M7]` *Decided — what each section holds:* Look (Font, size,
  BorderWidth, RootWeave, RootWordmark, RootLineTimeout, Effects), Frames
  (B8's six rows, Accent included, as B8 places it), Focus (FocusPolicy,
  FocusDelay, RaiseOnClick, Placement, RememberGeometry, SnapDistance,
  Modifier), Keys & buttons (Terminal, HintKeys), Desktops (desktop count,
  Panner, IconManager corner and sort, IconsOnRoot, Quiet), Outputs (Scale,
  XWayland), Privacy (Notifications; capture markers and prompts shown as
  facts, not settings — CAP-2, TRU-4), Menus. Bindings, Output rules and
  menus are listed read-only from the running config (built-in rows in
  `dim`) with an `Edit .twmrc by hand` button that opens `$EDITOR` in the
  Terminal: their structure is the file's job (P-6). The preview (SET-4)
  shows under Look and Frames, whose values it draws.
- **SET-12** `[M7]` *Decided — edits:* the window holds a list of pending
  key edits, applied to the file's text on top of whatever is on disk; an
  edit that changes nothing disappears (the same text, or a key the file
  lacks set to its default). `N changes` counts changed keys. Keys made of
  several values (`Font`, `Panner`, `IconManager`) are written whole. A row
  inside a block is edited in place (`SqueezeTitle`'s `"*"`), other block
  values are replaced. The diff shows 2 lines of context, `…` between hunks,
  new-file line numbers (old ones for removed lines); tabs show as spaces.
  `Write .twmrc` is disabled while nothing changed or `twm --check` fails;
  the failure reads `line N: message` in `alert`.
- **SET-13** `[M7]` *Decided — writing:* a temporary file beside the real
  one (through a symlink, so a dotfile manager's link stays a link), the
  file's mode kept, then `rename`; twm reloads at once rather than waiting
  for the watcher. Without a `~/.twmrc` the first write creates it with only
  the changed keys (the built-in defaults still apply, CFG-14).
- **SET-14** `[M7 · P-8]` *Decided — keys:* Up/Down choose a row, Left/Right
  change it (Shift: 5 slider steps), Space ticks, Return edits a text field
  or the hex (Return commits, Esc cancels), Tab / Shift-Tab change section,
  typing filters (Backspace, Esc clears), `Ctrl-S` writes, `Ctrl-D`
  discards. `M-…` keys other than `M-z` stay with the bindings. B1 on a
  slider sets it where pressed and again where released: twm's own windows
  see presses, not motion.

### GEN — general

- **GEN-1** `[all · P-3]` No animation exceeds 80 ms. `Effects none` is the
  only value (accepted because B0's sample config uses it).
- **GEN-2** `[all · P-4]` twm draws no panel, dock or tray. *Decided:*
  wlr-layer-shell is supported `[M4]` so screen lockers, launchers and on-screen
  displays work; twm itself never draws a bar. *Decided (M4):* the background
  layer sits above the weave and below the root text, so a wallpaper never
  hides the root line or icons; exclusive zones shrink the usable area that
  placement, B3 fit, snapping and twm's own windows use; an exclusive
  keyboard grab (launcher, locker) keeps focus until it unmaps.
- **GEN-3** `[M1 · B1]` Fractional scale per output (`Scale auto`, or
  `Output "DP-1" { scale 1.25 }`). Tokens are logical pixels. Text is
  rasterized at the output's scale. 1 px lines snap to the device grid and
  are `max(1, floor(scale))` device pixels thick.
- **GEN-4** `[M0]` Runs nested (Wayland and X11 backends), on a TTY (DRM,
  libinput, libseat), and headless.
- **GEN-5** `[M0]` Builds and runs on Linux and FreeBSD.
- **GEN-6** `[M0]` XWayland starts lazily on first X client; `XWayland off`
  disables it.
- **GEN-7** `[M0]` `twm --version` prints `twm (Tom's WM) <version>`;
  `twm --help` lists the options.
- **GEN-8** `[M1 · P-8]` *Decided:* `M` means the logo key (Mod4) by default;
  `Modifier Alt` switches to Alt. Reason: Alt chords like `M-;`, `M-b`, `M-f`
  belong to Emacs and readline inside terminals.

### KEY — default bindings

Pointer (FRM-12, OUT-8, OUT-9, ROOT-4): see those requirements.

| Key | Function | Source |
|-----|----------|--------|
| `M-Return` | `!"foot"` (terminal) | B0 |
| `M-b` | browser | B0 menu |
| `M-f` | files | B0 menu |
| `M-r` | `f.resize` (keyboard) | B0 menu |
| `M-i` | `f.iconify` | B0 menu |
| `M-space` | `f.raiselower` | B0 menu |
| `M-S-r` | `f.reload` | B0 menu |
| `M-S-q` | `f.quit` | B0 menu |
| `M-;` | `f.hints` | B4 |
| `M-z` / `M-S-z` | `f.undo` / `f.redo` | B3 |
| `M-u` | `f.urgent` | B7 |
| `M-n` | `f.log` | B7 |
| `M-Tab` | `f.nexttab` | B2 |
| `M-m` | `f.menu "main"` | decided |
| `M-S-m` | `f.move` (keyboard) | decided |
| `M-S-h` | `f.history` | decided |
| `M-,` (`"M-comma"`) | `f.settings` | decided (M7) |
| `M-d` | `f.run` | decided (IMG-13) |
| `M-w`, `M-S-w` | `f.delete`, `f.destroy` | decided (FRM-19) |
| `M-Up`, `M-S-f` | `f.fullzoom`, `f.fullscreen` | decided (ZOM-9) |
| `Print`, `M-Print` | `f.screenshot`, `f.fetchshot` | owner (SHOT-1, SHOT-2) |
| `M-l` | `f.lock` | decided (GRT-7) |
| volume, mute, mic and brightness keys | `f.volume …`, `f.brightness …` | decided (LVL-1) |
| `M-1`…`M-9`, `M-S-1`…`M-S-9` | desktop switch / send | decided |

---

## 4. Technical decisions

- **D-1 Language and build.** C17, meson ≥ 1.0, warnings as errors in CI.
  POSIX + the BSD/Linux common subset only; platform code (file watching,
  peer credentials) behind small modules.
- **D-2 wlroots 0.20.** The current stable series is 0.20 (0.20.2, July 2026).
  Debian 13 packages only 0.18, so meson looks for system `wlroots-0.20` and
  otherwise builds it as a pinned subproject (`subprojects/wlroots.wrap`).
  Arch and FreeBSD use their packages. wlroots breaks its API every minor
  release; moving to 0.21 is a deliberate change of its own.
- **D-3 Text: fcft + pixman.** fcft rasterizes glyphs straight into pixman
  images, the same path the frame code uses for the hatch, weave and glyphs,
  so every piece of chrome is a pixman image handed to the scene graph as a
  buffer. It pulls in only fontconfig and freetype (plus harfbuzz and utf8proc
  for shaping), is packaged on Debian, Arch and FreeBSD, and foot shows its
  quality. Pango/cairo would add GLib/GObject and a second rasterizer to draw
  single-line labels; its gains (bidi, paragraph layout) don't apply to tabs
  and menus. Known gap: right-to-left titles render in logical order. If that
  matters later, adding fribidi is a dependency question for the owner.
  Tracking is applied from glyph advances; text is grayscale-antialiased.
- **D-4 Drawing.** Solid fills and 1 px lines are `wlr_scene_rect`s; text,
  hatch, dashes, weave and glyphs are pixman-backed `wlr_buffer`s in
  `wlr_scene_buffer` nodes, re-rendered only when their content or scale
  changes. Scene layers, back to front: root, root line/icons, windows,
  utility frames, XWayland override-redirect windows, trusted dim, trusted
  frames, outline/readout/guides, menus, cursor.
- **D-5 Modules.** Pure logic (tab layout, focus intent, outline geometry and
  readout text, snapping, config parsing, history) lives in modules with no
  wlroots dependency, so it is unit-tested without a compositor.
- **D-6 Protocols, early.** xdg-shell, xdg-decoration, KDE server-decoration,
  xdg-activation, viewporter, fractional-scale, presentation-time,
  data-device, primary-selection, single-pixel-buffer, cursor-shape,
  wlr-screencopy and ext-image-copy-capture, virtual-pointer and
  virtual-keyboard (tests and remote tools; never reach trusted frames,
  TRU-4). Later: layer-shell, session-lock, pointer constraints.
- **D-7 Tests.** Unit tests for the pure modules. Screenshot tests run the
  real compositor on the headless backend with the pixman renderer
  (`WLR_BACKENDS=headless WLR_RENDERER=pixman`), one 1920×1080 output at
  scale 1. A small test client (wayland-client only) opens windows, drives the
  pointer through virtual-pointer — so click-to-place and the focus delay run
  through the real input path — and captures the output with screencopy. It
  writes PNG with a built-in stored-deflate encoder (no libpng), then checks
  pixel probes named after requirement IDs (tab height, box widths, separator
  columns, hatch period, border colors, dash pattern). Text pixels are
  checked for presence, not exact glyph shapes, because font builds differ.
- **D-8 IPC.** One UNIX socket (CFG-12), line-based, for `--reload` and
  `--choose` (whose request runs to an empty line and is answered when the
  prompt closes; hanging up withdraws the prompt).
- **D-10 D-Bus** `[M6]`. Through the sd-bus API: libsystemd on Linux, basu on
  FreeBSD (owner's choice); `-Dportal=disabled` builds without. Without a
  session bus, twm skips all of it. It serves the portal backend (TRU-8) and,
  when the name is free and `Notifications` is on, `org.freedesktop.
  Notifications`: each notification is one root line (`HH:MM  app  summary —
  body`), critical ones in `accent`; no actions, no icons, no toasts
  (ROOT-7). *Decided:* when twm is the session — not nested in another
  Wayland or X11 session, not headless — it sets `XDG_CURRENT_DESKTOP=twm`
  and pushes `WAYLAND_DISPLAY`, `DISPLAY`, `XDG_CURRENT_DESKTOP` and
  `TWM_SOCK` into the D-Bus and systemd activation environments, so portals
  started on demand find this display. Nested, it leaves the outer session's
  environment alone. `data/twm-portals.conf` routes Access to twm and
  ScreenCast/Screenshot to xdpw; screenshot tests run on a private bus.
- **D-11 PNG.** Screenshots are written by twm's own deflate (fixed
  Huffman codes over LZ77 matches), enough for flat chrome to compress well;
  reading a shared image needs only its chunks. No zlib (SHOT-4).
- **D-12 PAM** `[owner, 2026-10-24]`. The lock and the greeter link libpam
  (Linux-PAM, FreeBSD's OpenPAM: one API), for GRT-6. Approved by the owner.
- **D-9 Config file watching.** Watch the containing directory (inotify /
  kqueue) and re-read when the file's identity or mtime changes.

---

## 5. `.twmrc` key registry

Defaults are the built-in values used without a file. This table grows with
each milestone; CFG-3 says an unlisted key is an error.

| Key | Default | Values | Req |
|-----|---------|--------|-----|
| `Theme` | `"phosphor"` | `"phosphor"` | §2.1 |
| `Accent` | `#FFB000` | `#RRGGBB` | §2.1 |
| `Font` | `"IBM Plex Mono" 12` | family, size in px | §2.2 |
| `TitleHeight` | `26` | 22–34 | FRM-3 |
| `BorderWidth` | `1` | 1–4 | FRM-2 |
| `TitleHighlight` | `on` | on/off | FRM-10 |
| `SqueezeTitle` | `{ "*" left }` | pattern → left/center/right/off | FRM-4 |
| `NoTitle` | `{ }` | app-id patterns | FRM-15 |
| `RootWeave` | `on` | on/off | ROOT-1 |
| `RootWordmark` | `on` | on/off | ROOT-2 |
| `Effects` | `none` | `none` | GEN-1 |
| `Scale` | `auto` | auto / number | GEN-3 |
| `Output` | — | `"name" { scale mode position }` | GEN-3 |
| `FocusPolicy` | `sloppy` | sloppy/click | FOC-1 |
| `FocusDelay` | `80` | ms, 0–1000 | FOC-2 |
| `RaiseOnClick` | `off` | on/off | FOC-6 |
| `OpaqueMove` | `off` | on/off | OUT-1 |
| `Placement` | `interactive` | interactive/cascade | OUT-12 |
| `RememberGeometry` | `on` | on/off | OUT-16 |
| `Snap` | `{ edges thirds }` | edges, thirds, center | SNP-1 |
| `SnapDistance` | `12` | px | SNP-2 |
| `Modifier` | `Mod4` | Mod4/Alt | GEN-8 |
| `Terminal` | `"foot"` | command | IMG-9 |
| `HintKeys` | `"asdfjklgh"` | letters | HNT-2 |
| `RootLineTimeout` | `60` | seconds | ROOT-6 |
| `IconManager` | `{ at top-right sort name }` | corner, sort | IMG-1 |
| `IconsOnRoot` | `on` | on/off | ROOT-9 |
| `IdleLock` | `600` | seconds, `0` never | GRT-7 |
| `IdleScreen` | `900` | seconds, `0` never | GRT-7 |
| `LockOnSleep` | `on` | on/off | GRT-7 |
| `RootCard` | `off` | on/off, or `{ at <corner> fields host uptime load pool mem frames }` | CARD-1 |
| `Panner` | `{ desktops 4 }` | `{ desktops N }`, `{ desktops N off }` (keep N, hide the Panner), on/off | DSK-1 |
| `Quiet` | `{ }` | desktop numbers | URG-3 |
| `XWayland` | `on` | on/off (takes effect at the next start) | GEN-6 |
| `Notifications` | `on` | on/off: serve `org.freedesktop.Notifications` as root lines, if no one else does | D-10 |
| `TouchpadTap` | `on` | on/off | INP-1 |
| `TouchpadTapDrag` | `on` | on/off | INP-1 |
| `TouchpadNaturalScroll` | `off` | on/off | INP-1 |
| `TouchpadDisableWhileTyping` | `on` | on/off | INP-1 |
| `TouchpadScroll` | `two-finger` | two-finger/edge/none | INP-1 |
| `TouchpadClick` | `default` | default/areas/clickfinger | INP-1 |
| `TouchpadSpeed` | `0` | −1 … 1 | INP-1 |
| `TouchpadAccel` | `adaptive` | adaptive/flat | INP-1 |
| `Touchscreen` | `on` | on/off | INP-3 |
| `NoDefaultBindings` | — | no value | CFG-14 |
| `CloseBox` | `on` | on/off | FRM-19 |
| `VolumeStep` | `5` | percent, 1–50 | LVL-1 |
| `BrightnessStep` | `5` | percent, 1–50 | LVL-1 |
| `ButtonN = context : function` | see KEY | contexts: root, title, iconify, resize, close, window, icon, iconmgr | FRM-12 |
| `"key" : function` | see KEY | | KEY |
| `Menu` | `"main"` per MNU-4 | | MNU-7 |

### 5.1 Syntax

One file, read top to bottom. Blank lines and comments are free.

- **Comments** start with `#` and run to the end of the line. Exception, so
  that B0's `Accent #FFB000` reads: `#` followed by exactly six hex digits
  and then a space, a tab, the end of the line or `}` is a color.
- **Values**: bare words (`sloppy`, `top-right`, `f.move`), quoted strings
  (`"IBM Plex Mono"`, with `\"` `\\` `\n` escapes), numbers (`26`, `1.25`),
  colors (`#7FD1AE`), `!"command"` (shorthand for `f.exec "command"`), and
  blocks `{ … }`, which may span lines; inside a block each line is a row.
- **Settings**: `Key value…` on one line. Every key is in the §5 registry.
- **Button bindings**: `ButtonN = [modifiers :] context : function`.
  `N` is 1–5. Contexts: `root`, `title`, `iconify`, `resize`, `window`,
  `border`, `icon`, `iconmgr`. Modifiers are letters, optionally joined by
  `-`: `M` (the `Modifier` key), `S` Shift, `C` Control, `A` Alt, `L` logo.
- **Key bindings**: `"keyspec" : function`. A key spec is modifier letters
  with `-`, then the unshifted key: an xkb name (`Return`, `space`, `Tab`)
  or one character (`;`, `1`, `b`): `"M-S-1"`, `"M-;"`. Caps Lock and Num
  Lock are ignored when matching.
- **Menus**: `Menu "name" { "label" function … }`, one item per line;
  `"" f.separator` draws a rule.
- **Functions**: `f.nop`, `f.exec "cmd"`, `f.terminal`, `f.menu "name"`,
  `f.move`, `f.resize`, `f.raisemove`, `f.squeeze`, `f.iconify`, `f.raise`,
  `f.lower`, `f.raiselower`, `f.focus`, `f.delete`, `f.reload`, `f.quit`,
  `f.hints`, `f.undo`, `f.redo`, `f.urgent`, `f.log`, `f.nexttab`,
  `f.history`, `f.desktop N`, `f.send N`, `f.separator`, `f.zoom`,
  `f.horizoom`, `f.fullzoom`, `f.fullscreen` (ZOM-1, ZOM-3),
  `f.screenshot`, `f.fetchshot` (SHOT), `f.lock`, `f.suspend` (GRT-7, GRT-9). Functions whose
  machinery belongs to a later milestone say so on the root line.

The built-in defaults are themselves written in this syntax
(`config_default_twmrc` in `src/settings.c`).

---

## 6. Readings of the boards

Places where a board could be read more than one way, and the reading taken.

1. **Hatch only on focus** (FRM-11): B0's unfocused tabs are narrower, so the
   tab width changes on focus.
2. **No resize box on xclock** (FRM-6): read as "fixed-size windows can't be
   resized".
3. **`f.squeeze`** (FRM-14): shade on a single window (ctwm), fold on a group
   (B2).
4. **Geometry readout** (OUT-4): `560×300+380+690` on B0 matches the
   outline's outer box in desktop pixels, title included.
5. **`Font "IBM Plex Mono" 11`** in B0's sample config, while every tab on B0
   is set at 12 px: the size is read as pixels and the default is 12. The
   sample's 11 is a user's own choice and stays valid.
6. **"per window" undo** (HIS-2): undo scoped to the focused window.
7. **"nearest window first"** (HNT-2): nearest to the pointer.
8. **"in foot"** on a RUN row (IMG-9): executables run in the terminal.
9. **"dims by one step"** (TRU-4): a single 35 % overlay.
10. **Group buttons in graphite** (GRP-3, B2): buttons take the colors of the
    tab beside them.

---

## 7. Open questions for the owner

- **License** — *decided:* MIT.
- **Future dependencies** — *Decided:* sd-bus (libsystemd / basu) for M6;
  libpam for the lock and the greeter (D-12, 2026-10-24).
  Still open: **PipeWire**, for twm's own ScreenCast backend (marking the app
  that shares, not xdpw; camera and microphone markers, CAP-5), and
  **libpolkit-agent**, for password prompts in the trusted frame (TRU-8).
- **Settings window** (M7) — *decided:* drawn by the compositor (SET-10).
- **Binary name** — `/usr/bin/twm` clashes with the X11 `twm` package on
  distributions; packaging will need a conflict or an alternative.
