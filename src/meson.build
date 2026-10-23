# Pure modules (D-5): no wlroots, unit-tested on their own.
twm_core_src = files(
  'draw.c',
  'theme.c',
)

twm_src = twm_core_src + files(
  'input.c',
  'main.c',
  'output.c',
  'pixbuf.c',
  'root.c',
  'server.c',
  'view.c',
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
