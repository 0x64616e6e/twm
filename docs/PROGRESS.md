# Progress

Newest milestone first. Requirement IDs refer to [DESIGN.md](DESIGN.md).

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
