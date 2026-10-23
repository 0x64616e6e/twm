# Tom's WM

A Wayland compositor that reads twm (Tom LaStrange, 1987) for today:
squeezed title tabs, 1 px frames, an Icon Manager instead of a panel, root
menus, outline moves, and one plain-text `~/.twmrc`. The binary is `twm`.

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
fcft (fontconfig, freetype, harfbuzz), libinput; Xwayland for X11 clients.
The UI font is IBM Plex Mono.

## Run

- Nested: run `./build/src/twm` inside a Wayland or X11 session.
- On a TTY: run it from a virtual console (needs seatd or logind).
- Headless: `./build/src/twm --headless 1920x1080` (used by the tests).

`twm -s <cmd>` runs a command once the compositor is up. `M-S-q` quits,
`M-Return` opens `foot` (`M` is the logo key).

## Test

```sh
meson test -C build --suite twm
```

Screenshot tests run twm on the headless backend with the pixman renderer and
drive it over Wayland (virtual pointer in, screencopy out). Screenshots land in
`build/tests/*.png`.

## License

MIT, see [LICENSE](LICENSE).
