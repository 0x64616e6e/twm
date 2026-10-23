# Progress

Newest milestone first. Requirement IDs refer to [DESIGN.md](DESIGN.md).

## M1 — Frames (2026-10-23)

**Done**

- Frames (FRM-1…3, 5…12, 15…17, 19): squeezed tab with iconify box, title,
  highlight hatch and resize box, drawn as one pixman buffer; 1 px border.
  Focused, unfocused and urgent states, the urgent border dashed 3/3 with a
  continuous phase. Fixed-size clients get no resize box.
- Text with fcft (D-3): IBM Plex Mono at the output scale, cut with "…"
  when it doesn't fit.
- Server-side decorations always (FRM-15): xdg-decoration and the KDE
  protocol.
- Sloppy focus with intent (FOC-1…10): keyboard focus moves after the
  pointer rests 80 ms on a frame; pointer focus is immediate; a press focuses
  at once; focus never raises.
- Outlines (OUT-1…10): B2 on a title, B1-drag on a title, `M-B1`, resize box,
  `M-B3` by 3×3 grid, `M-S-m` / `M-r` from the keyboard. Rule-of-thirds
  outline with the window's name and a live `W×H+X+Y` readout; the client is
  configured once, on release. `Esc` cancels, `Return` commits, arrows nudge.
- Click-to-place (OUT-12, 14, 15): new windows wait with their outline
  hanging from the pointer; B1 places, B2-drag sizes, `Esc` places where it
  is; a queue for several; transients open over their parent; X11
  `USPosition` is kept.
- Urgency (URG-2, M1 part): an xdg-activation request whose token isn't tied
  to input on the focused client marks the window urgent; the X11 urgency
  hint does too. Focusing clears it.
- Root wordmark and status line (ROOT-2, ROOT-3).
- Pure modules with unit tests: `squeeze.c` (tab layout), `geometry.c`,
  `intent.c` (focus state machine on a fake clock).

**Screenshot test** `shot-m1_anatomy`: 91 checks. B0's windows are placed by
clicking where B0 puts them, then checked pixel by pixel against the frame
anatomy (box sizes, separators, hatch period and 8 px gap, borders, the 80 px
focus growth, the dash pattern), plus focus timing (a 25 ms crossing doesn't
move focus, an 80 ms rest does, the root never takes it), the placement
outline and readout, a mid-drag move that leaves the window in place, and a
resize that reaches the client once. Stable over repeated runs and under CPU
load.

![M1 desktop: B0 recreated by the test](shots/m1-desktop.png)

![Focused tab, 3× crop](shots/m1-tabs.png)

**Decided along the way** (recorded in DESIGN.md)

- FRM-11: B0's 0.3 px letter-spacing on the focused title is dropped. With
  it, the title shifted by ~9 px on every focus change; now only the hatch
  and its gap appear (exactly 80 px).
- FRM-5: when a title doesn't fit, it is cut before the hatch shrinks, since
  the hatch is the focus texture (P-5).
- Transients take focus if their parent had it (OUT-15).

**Not done yet / not verified**

- FRM-13 iconify and FRM-14 squeeze log "not available yet" (M4).
- `SqueezeTitle` center/right/off is implemented in the layout code and
  unit-tested, but only `left` is reachable until the config exists (M2).
- The keyboard paths (`M-S-m`, `M-r`, `Esc`, `Return`, arrows) are not
  covered by the screenshot test: the test client has no virtual keyboard
  yet.
- Resizing from the top or left edge moves the frame before the client has
  committed its new size, so it can jump for a frame.
- GEN-3: frames are drawn at the largest output scale, but 1 px lines are
  not yet snapped to the device grid at fractional scales; nothing has been
  tested at a scale other than 1.
- Text uses per-character kerning; harfbuzz text-run shaping (D-3) is not
  wired in yet.
- Instrument Serif isn't installed on the dev machine, so the wordmark falls
  back to the system serif italic; IBM Plex Mono is used for everything else.
- CI still hasn't run (no remote).

## M0 — Skeleton (2026-10-23)

**Done**

- meson build, C17. wlroots 0.20.2 comes from the system when available,
  otherwise from a pinned subproject together with the wayland 1.26.0,
  libdrm 2.4.134 and pixman 0.46.4 releases it needs (D-2). Debian 13 takes
  that path; Arch uses its packages.
- tinywl's structure split into modules: `server`, `output`, `input`, `view`
  (one type over xdg-shell and XWayland), `xdg`, `xwayland`, `root`, `draw`,
  `pixbuf`, `theme`.
- GEN-4: runs nested under Wayland and X11 (checked by hand), on a TTY
  through wlroots' session support (not yet tried on hardware), and headless
  with `--headless WxH`.
- GEN-6: XWayland starts lazily; `--no-xwayland` turns it off.
- GEN-7: `twm --version`, `twm --help`.
- ROOT-1: every output's root is `ground` with the 6 px dot weave, drawn
  into a pixman buffer at the output's scale and kept continuous across
  outputs.
- `-s <cmd>` and `--exit-with-startup` (exit with the command's status),
  which the screenshot tests use.
- Test harness (D-7): a Wayland test client (xdg-shell windows,
  virtual-pointer input, screencopy capture, PNG writer with no libpng),
  `tests/run-shot.sh`, unit tests for the pure modules.
- CI workflows for Arch, Debian 13 (subproject path) and FreeBSD 14.3.

**Screenshot test** `shot-m0_root`: 11 checks — output size, weave colors and
period over a 60×60 patch and at the far corner, and a client window
reaching the screen.

![M0 root weave and a window corner, 4× crop](shots/m0-root.png)

**Not verified**

- CI has not run yet: the repository has no remote. The FreeBSD job's
  package names (`wlroots020`, `ibm-plex`) are a best guess; the job falls
  back to the subproject when the wlroots package is missing.
- TTY/DRM start on real hardware.

**Found along the way**

- pixman's solid fills don't clip to the image. The weave overflowed its
  buffer on outputs whose size isn't a multiple of 6 (seen nested in X11 at
  1024×768). All fills now clip; `unit-draw` guards it.
- wlroots 0.20's pixman renderer requires pixman ≥ 0.46 while its top-level
  check accepts 0.43, so twm resolves pixman before wlroots.
