# Unit tests: pure modules, no compositor (D-5).
foreach name : ['theme', 'draw', 'squeeze', 'geometry', 'intent']
  test('unit-' + name, executable(
    'test_' + name,
    'unit/test_@0@.c'.format(name),
    twm_core_src,
    include_directories: [config_inc, include_directories('../src')],
    dependencies: [pixman, math],
  ), suite: 'unit')
endforeach

# Screenshot tests: twm on the headless backend, driven over Wayland (D-7).
test_client = static_library(
  'tc',
  'client/client.c',
  dependencies: [wayland_client, client_protos, math],
)
run_shot = find_program('run-shot.sh')

foreach name : ['m0_root']
  prog = executable(
    'shot_' + name,
    'shot/@0@.c'.format(name),
    link_with: test_client,
    include_directories: include_directories('client'),
    dependencies: [wayland_client, client_protos],
  )
  test('shot-' + name, run_shot,
    args: [twm_exe, prog, meson.current_build_dir() / name + '.png'],
    depends: [twm_exe, prog],
    suite: 'shot',
    timeout: 60,
  )
endforeach
