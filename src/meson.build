# Pure modules (D-5): no wlroots, unit-tested on their own.
twm_core_src = files(
  'apps.c',
  'draw.c',
  'fetchfmt.c',
  'geomem.c',
  'geometry.c',
  'hintcode.c',
  'history.c',
  'ipcclient.c',
  'intent.c',
  'levels.c',
  'rcedit.c',
  'squeeze.c',
  'settings.c',
  'snap.c',
  'sysinfo.c',
  'theme.c',
  'twmrc.c',
)

twm_src = twm_core_src + files(
  'actions.c',
  'capture.c',
  'card.c',
  'prompt.c',
  'decoration.c',
  'desktop.c',
  'focus.c',
  'frame.c',
  'grab.c',
  'group.c',
  'hints.c',
  'histview.c',
  'icons.c',
  'imgr.c',
  'info.c',
  'input.c',
  'internal.c',
  'ipc.c',
  'layers.c',
  'logview.c',
  'main.c',
  'media.c',
  'menu.c',
  'outline.c',
  'output.c',
  'panner.c',
  'pixbuf.c',
  'rc.c',
  'remember.c',
  'root.c',
  'rootline.c',
  'server.c',
  'setwin.c',
  'text.c',
  'touch.c',
  'ui.c',
  'view.c',
  'watch.c',
  'xdg.c',
  'zoom.c',
)
if have_xwayland
  twm_src += files('xwayland.c')
endif
twm_deps = [wlroots, wayland_server, server_protos, xkbcommon, pixman, fcft, libinput, math]
if have_portal
  twm_src += files('portal.c')
  twm_deps += sdbus
endif

twm_exe = executable(
  'twm',
  twm_src,
  dependencies: twm_deps,
  include_directories: config_inc,
  install: true,
)

# FET-*: twmfetch, the machine's facts and Tom's WM's own.
twmfetch_exe = executable(
  'twmfetch',
  files('fetch.c', 'fetchfmt.c', 'sysinfo.c', 'ipcclient.c', 'history.c', 'theme.c'),
  dependencies: [math],
  include_directories: config_inc,
  install: true,
)
