project(
  'twm',
  'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=1.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'buildtype=debugoptimized',
    'default_library=static',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
    '-Wno-unused-parameter',
    '-Wno-missing-field-initializers',
    '-Wundef',
    '-Wmissing-prototypes',
    '-Wstrict-prototypes',
    '-Wshadow',
    '-Wvla',
  ]),
  language: 'c',
)

# wlroots 0.20's pixman renderer needs pixman >= 0.46 but its top level
# accepts older ones without falling back, so settle pixman first.
pixman = dependency('pixman-1', version: '>=0.46.0', fallback: 'pixman',
  default_options: ['werror=false', 'tests=disabled', 'demos=disabled'])

# D-2: wlroots 0.20. Use the system package when it exists, otherwise build
# the pinned subproject (subprojects/wlroots.wrap) as a static library. Its
# `xwayland` option yields to ours.
wlroots = dependency(
  'wlroots-0.20',
  version: ['>=0.20.0', '<0.21.0'],
  fallback: 'wlroots',
  default_options: ['examples=false'],
)

# Same minimums as wlroots 0.20, so a subproject build of wlroots and twm link
# the same libwayland and pixman.
wayland_server = dependency('wayland-server', version: '>=1.24')
wayland_client = dependency('wayland-client', version: '>=1.24')
wayland_protos = dependency('wayland-protocols', version: '>=1.41')
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput', version: '>=1.21')
fcft = dependency('fcft', version: ['>=3.0.0', '<4.0.0'], include_type: 'system')
math = cc.find_library('m', required: false)

have_xwayland = wlroots.get_variable(pkgconfig: 'have_xwayland', internal: 'have_xwayland') == 'true'
if get_option('xwayland').enabled() and not have_xwayland
  error('xwayland was requested but wlroots was built without it')
endif

# M6: D-Bus through the sd-bus API, from libsystemd on Linux or basu on
# FreeBSD (same API, different header directory).
sdbus = dependency('libsystemd', version: '>=239', required: false)
sdbus_header = 'systemd/sd-bus.h'
if not sdbus.found()
  sdbus = dependency('basu', required: false)
  sdbus_header = 'basu/sd-bus.h'
endif
have_portal = sdbus.found() and not get_option('portal').disabled()
if get_option('portal').enabled() and not sdbus.found()
  error('portal was requested but neither libsystemd nor basu was found')
endif

# GRT-6, D-12: PAM for twm's own lock and greeter (Linux-PAM or OpenPAM).
pam = dependency('pam', required: false)
if not pam.found() and cc.has_header('security/pam_appl.h')
  pam = cc.find_library('pam', required: false)
endif
have_pam = pam.found() and not get_option('lock').disabled()
if get_option('lock').enabled() and not pam.found()
  error('lock was requested but PAM (security/pam_appl.h, libpam) was not found')
endif

conf = configuration_data()
conf.set10('HAVE_PAM', have_pam)
conf.set_quoted('TWM_VERSION', meson.project_version())
conf.set10('HAVE_XWAYLAND', have_xwayland)
conf.set10('HAVE_PORTAL', have_portal)
if have_portal
  conf.set('SDBUS_HEADER', '<' + sdbus_header + '>')
endif
configure_file(output: 'config.h', configuration: conf)
config_inc = include_directories('.')

subdir('protocols')
subdir('src')

# The session: a wrapper that sets the environment, and an entry display
# managers list (wayland-sessions).
install_data('data/twm-session', install_dir: get_option('bindir'), install_mode: 'rwxr-xr-x')
install_data('data/twm.desktop', install_dir: get_option('datadir') / 'wayland-sessions')

# GRT-6: the `twm` PAM service, on the system's own auth stack. PAM reads
# /etc/pam.d whatever the prefix.
if have_pam
  if host_machine.system() == 'freebsd'
    pam_stack = 'auth\t\tinclude\t\tsystem\naccount\t\tinclude\t\tsystem\n'
  elif import('fs').exists('/etc/pam.d/common-auth')
    pam_stack = '@include common-auth\n@include common-account\n'
  else
    pam_stack = 'auth\t\tinclude\t\tsystem-auth\naccount\t\tinclude\t\tsystem-auth\n'
  endif
  pam_file = configure_file(output: 'twm.pam', configuration: {'STACK': pam_stack},
    input: 'data/twm.pam.in')
  install_data(pam_file, install_dir: '/etc/pam.d', rename: 'twm', install_tag: 'pam')
endif

# Portal files: xdg-desktop-portal finds twm's backend and the per-desktop
# preferences here; the xdpw chooser config is an example to copy.
if have_portal
  install_data('data/twm.portal',
    install_dir: get_option('datadir') / 'xdg-desktop-portal' / 'portals')
  install_data('data/twm-portals.conf',
    install_dir: get_option('datadir') / 'xdg-desktop-portal')
  install_data('data/xdg-desktop-portal-wlr.conf',
    install_dir: get_option('datadir') / 'doc' / 'twm')
endif

if get_option('tests')
  subdir('tests')
endif

summary({
  'wlroots': wlroots.version(),
  'xwayland': have_xwayland,
  'portal': have_portal ? sdbus.name() : false,
  'tests': get_option('tests'),
}, bool_yn: true)
