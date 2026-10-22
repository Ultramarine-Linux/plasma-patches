build_dir := "build"
build_type := "RelWithDebInfo"
prefix := "/usr"

default: build

# configure the cmake build directory
configure type=build_type pfx=prefix:
    cmake -S . -B {{build_dir}} \
        -DCMAKE_BUILD_TYPE={{type}} \
        -DCMAKE_INSTALL_PREFIX={{pfx}}
    ln -sf {{build_dir}}/compile_commands.json compile_commands.json

# build everything (configures first if needed)
build:
    @[ -f {{build_dir}}/CMakeCache.txt ] || just build_dir={{build_dir}} build_type={{build_type}} prefix={{prefix}} configure
    cmake --build {{build_dir}} --parallel

# run the test suite
test: build
    ctest --test-dir {{build_dir}} --output-on-failure

# install system-wide (run with sudo)
install:
    cmake --install {{build_dir}}

# configure, build and install into ~/.local
install-user:
    just build_dir={{build_dir}}-user configure {{build_type}} "$HOME/.local"
    just build_dir={{build_dir}}-user build
    cmake --install {{build_dir}}-user

# remove build directories
clean:
    rm -rf {{build_dir}} {{build_dir}}-user compile_commands.json
