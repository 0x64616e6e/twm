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
  SemiBold). Fallback: fontconfig `monospace`.
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
  (ROOT-9). Until M4 the iconify box is drawn and the binding logs that the
  function is not available yet.
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
- **FRM-18** `[M4 · B0]` Utility frames (Icon Manager, Panner, history, log)
  have a title-only tab with 12 padding and no buttons; the same state colors
  apply.
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
  other frame and stays on the output.
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
  Windows without an app id, transients and windows whose remembered output
  is gone are placed as usual.

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
- **HIS-3** `[M3 · B3]` `f.history` (default `M-S-h`, decided) toggles a
  utility frame `history`: rows of operation (ink-2), target (ink) and age
  (dim, `2m`). An outline operation in progress shows on top as `pending`:
  operation in `accent`, `accent-wash-soft` background, 3 px inset `accent`
  bar on the left. Footer: `M-z undo · M-S-z redo · per window`.
- **HIS-4** `[M3]` History keeps the last 100 operations for the session and is
  not saved.
- **HIS-5** `[M7 · B8]` A write from the Settings window is undoable with
  `M-z` while Settings has focus.

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
  root-line messages.
- **ROOT-9** `[M4 · B0]` Root icons: each iconified window gets an icon,
  laid out left to right from (72, H − 140) in 72 px slots 20 px apart. Box
  56 × 56, `surface`, 1 px `hairline`; glyph 28 px in `ink-2` (the app icon in
  one color if available, else the app id's first letter in Plex Mono 600 at
  20 px); label below in 11 px `ink-2` on `ground` with 4 px padding. Urgent:
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
  for the same function; menus never hard-code shortcut text.
- **MNU-4** `[M4 · B0]` Default `main` menu: xterm · browser · files |
  move · resize · squeeze · iconify · raise / lower | desktops ▸ ·
  reload .twmrc · exit.
- **MNU-5** `[M4]` Window functions run from a menu ask for a target: the
  cursor becomes a crosshair, the next B1 on a window applies, `Esc` cancels
  (twm).
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

### GRP — tab groups

- **GRP-1** `[M5 · B2]` B2-dragging a window's tab and releasing it on another
  frame's tab row or top edge joins the two in one frame.
- **GRP-2** `[M5 · B2]` Over a valid target the outline gives way to a
  floating tab (`graphite`, `ink` text, 1 px inset `accent`) under the
  pointer; the target row opens a dashed `accent` slot the width of the
  dragged tab at the insertion point (4 px margins), and `DROP → GROUP OF N`
  (11 px, tracked 1, `accent`) shows near the pointer.
- **GRP-3** `[M5 · B2]` A group frame has one border and one client area (all
  members get the same size) and one tab row: iconify box, the tabs, resize
  box. Each tab stays squeezed to its name. The active tab is focused-style
  with its hatch inside, 12 after the title; inactive tabs are `graphite`
  with `muted` 500 text and a 1 px `hairline` right separator. Each button
  takes the colors of the tab beside it.
- **GRP-4** `[M5 · B2]` B1 on an inactive tab activates it. `M-Tab` cycles
  the active tab within the focused group only.
- **GRP-5** `[M5 · B2]` B2-dragging a tab off the row splits it out; it is
  placed by outline like a new window (OUT-12).
- **GRP-6** `[M5 · B2]` B3 on a group's tab row folds every tab into the
  active one; B3 again fans them out.
- **GRP-7** `[M5]` Move, resize and iconify act on the whole group. The Icon
  Manager shows one row (IMG-3).

### HNT — hint mode

- **HNT-1** `[M5 · B4]` `M-;` enters hint mode: every visible tab on the
  current desktop shows a hint letter where its iconify box was (same box,
  `accent` fill, `hint-letter`). Nothing else changes and nothing moves.
- **HNT-2** `[M5 · B4]` Letters come from `HintKeys` (default `asdfjklgh`,
  home row first). *Decided:* "nearest first" means distance from the
  pointer to the frame; ties go to the higher window in the stack. More
  windows than letters → two-letter codes.
- **HNT-3** `[M5 · B4]` Typing a window's letter marks it selected for one
  frame (letter box `ground` with `accent` letter and a 2 px inset `accent`
  border; tab 1 px inset `accent`, `ink` text; frame `accent`), then the
  window raises and takes focus and hint mode ends.
- **HNT-4** `[M5 · B4]` `Esc`, or any key that is not a hint, leaves hint mode
  with nothing changed.
- **HNT-5** `[M5]` Hint mode holds the keyboard; no client gets keys until it
  ends.

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
  it is a dot, not a rounded corner, and is drawn as on B6.

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

### SET — settings, as a file

- **SET-1** `[M7 · B8]` A Settings window, `settings — ~/.twmrc`, edits the
  file. It is not a second source of truth.
- **SET-2** `[M7 · B8]` Sections: Look, Frames, Focus, Keys & buttons,
  Desktops, Outputs, Privacy, Menus; a `filter keys…` field narrows the
  controls. The nav footer reads `Everything here is also man 5 twmrc.`
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

### GEN — general

- **GEN-1** `[all · P-3]` No animation exceeds 80 ms. `Effects none` is the
  only value (accepted because B0's sample config uses it).
- **GEN-2** `[all · P-4]` twm draws no panel, dock or tray. *Decided:*
  wlr-layer-shell is supported `[M4]` so screen lockers, launchers and on-screen
  displays work; twm itself never draws a bar.
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
  future tools. No D-Bus in the compositor until M6 (see §7).
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
| `Panner` | `{ desktops 4 }` | desktops N, on/off | DSK-1 |
| `Quiet` | `{ }` | desktop numbers | URG-3 |
| `XWayland` | `on` | on/off (takes effect at the next start) | GEN-6 |
| `NoDefaultBindings` | — | no value | CFG-14 |
| `ButtonN = context : function` | see KEY | contexts: root, title, iconify, resize, window, icon, iconmgr | FRM-12 |
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
  `f.history`, `f.desktop N`, `f.send N`, `f.separator`. Functions whose
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

- **License** — undecided. MIT would match twm's X11 license and wlroots.
- **Future dependencies** — the M6 portal backend (screen-share picker,
  consent) and polkit prompts speak D-Bus, so M6 will need a D-Bus library
  (sd-bus, basu on FreeBSD, or libdbus). To be asked before M6.
- **Settings window** (M7) — drawn by the compositor like the Icon Manager
  (no new deps), or a separate client. Leaning compositor-drawn; decided at M7.
- **Binary name** — `/usr/bin/twm` clashes with the X11 `twm` package on
  distributions; packaging will need a conflict or an alternative.
