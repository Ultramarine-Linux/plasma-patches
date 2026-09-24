project(
    'ultramarine-kcms',
    version: '0.1.0',
    meson_version: '>= 0.61.0',
    license: 'GPL-3.0-only',
)

cmake_build_dir = join_paths(meson.project_build_root(), 'cmake-backend')
cmake_install_prefix = get_option('prefix')
cmake_build_type = get_option('cmake_build_type')

cmake_configure = run_command(
    'cmake',
    '-S', meson.project_source_root(),
    '-B', cmake_build_dir,
    '-DCMAKE_BUILD_TYPE=' + cmake_build_type,
    '-DCMAKE_INSTALL_PREFIX=' + cmake_install_prefix,
    check: true,
)

cmake_build = custom_target(
    'cmake-build',
    output: 'cmake-build.stamp',
    command: [
        'sh',
        '-c',
        'cmake --build "' + cmake_build_dir + '" --parallel && cmake -E touch "$1"',
        'meson-cmake-build',
        '@OUTPUT@',
    ],
    build_by_default: true,
    build_always_stale: true,
    console: true,
)

meson.add_install_script(
    'cmake',
    '--install',
    cmake_build_dir,
    skip_if_destdir: false,
)
