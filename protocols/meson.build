wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
wayland_scanner = find_program('wayland-scanner', native: true)

# Server side: wlroots headers include these generated headers.
server_protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  'wlr-layer-shell-unstable-v1.xml',
]

# Client side: the test client.
client_protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  wl_protocol_dir / 'staging/xdg-activation/xdg-activation-v1.xml',
  wl_protocol_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
  'wlr-screencopy-unstable-v1.xml',
  'wlr-virtual-pointer-unstable-v1.xml',
  'virtual-keyboard-unstable-v1.xml',
  'wlr-layer-shell-unstable-v1.xml',
]

server_protos_src = []
foreach xml : server_protocols
  server_protos_src += custom_target(
    xml.underscorify() + '_server_h',
    input: xml,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

client_protos_src = []
foreach xml : client_protocols
  client_protos_src += custom_target(
    xml.underscorify() + '_client_h',
    input: xml,
    output: '@BASENAME@-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  client_protos_src += custom_target(
    xml.underscorify() + '_client_c',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

server_protos = declare_dependency(sources: server_protos_src)
client_protos_lib = static_library(
  'client_protos',
  client_protos_src,
  dependencies: wayland_client,
)
client_protos = declare_dependency(
  link_with: client_protos_lib,
  sources: client_protos_src,
)
