# Unit tests: pure modules, no compositor (D-5).
foreach name : ['theme', 'draw', 'squeeze', 'geometry', 'intent', 'twmrc', 'settings', 'geomem', 'snap', 'history', 'group']
  test('unit-' + name, executable(
    'test_' + name,
    'unit/test_@0@.c'.format(name),
    twm_core_src,
    include_directories: [config_inc, include_directories('../src')],
    dependencies: [pixman, xkbcommon, math],
  ), args: [meson.current_source_dir() / 'data' / 'b0.twmrc'], suite: 'unit')
endforeach

# Screenshot tests: twm on the headless backend, driven over Wayland (D-7).
test_client = static_library(
  'tc',
  'client/client.c',
  dependencies: [wayland_client, client_protos, xkbcommon, math],
)
run_shot = find_program('run-shot.sh')

foreach name : ['m0_root', 'm1_anatomy', 'm2_config', 'm3_snap', 'm4_imgr', 'm4_menu', 'm5_groups', 'm6_calm']
  prog = executable(
    'shot_' + name,
    'shot/@0@.c'.format(name),
    link_with: test_client,
    include_directories: [config_inc, include_directories('client')],
    dependencies: [wayland_client, client_protos, xkbcommon] + (have_portal ? [sdbus] : []),
  )
  test('shot-' + name, run_shot,
    args: [twm_exe, prog, meson.current_build_dir() / name + '.png'],
    depends: [twm_exe, prog],
    suite: 'shot',
    timeout: 60,
  )
endforeach
