# Pure modules (D-5): no wlroots, unit-tested on their own.
twm_core_src = files(
  'draw.c',
  'geomem.c',
  'geometry.c',
  'intent.c',
  'squeeze.c',
  'settings.c',
  'theme.c',
  'twmrc.c',
)

twm_src = twm_core_src + files(
  'actions.c',
  'decoration.c',
  'focus.c',
  'frame.c',
  'grab.c',
  'input.c',
  'ipc.c',
  'main.c',
  'outline.c',
  'output.c',
  'pixbuf.c',
  'rc.c',
  'remember.c',
  'root.c',
  'rootline.c',
  'server.c',
  'text.c',
  'view.c',
  'watch.c',
  'xdg.c',
)
if have_xwayland
  twm_src += files('xwayland.c')
endif

twm_exe = executable(
  'twm',
  twm_src,
  dependencies: [wlroots, wayland_server, server_protos, xkbcommon, pixman, fcft, math],
  include_directories: config_inc,
  install: true,
)
