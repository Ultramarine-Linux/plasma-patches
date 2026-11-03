# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# "Launch Using Dedicated Graphics Card" in the app launchers,
# only on hybrid GPU systems (including laptops with an eGPU plugged in),
# see docs/dgpu-launch.md
set(_component COMPONENT ultramarine-dgpu-launch)

# like the paths in the top-level CMakeLists.txt, these follow the install prefix unless set with -D
if (NOT DEFINED DGPU_DETECT_EXECUTABLE)
    set(DGPU_DETECT_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-dgpu-detect")
endif ()
if (NOT DEFINED DGPU_LAUNCH_EXECUTABLE)
    set(DGPU_LAUNCH_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-dgpu-launch")
endif ()
if (NOT DEFINED UDEV_RULES_DIR)
    set(UDEV_RULES_DIR "${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d")
endif ()
set(SYSTEMCTL_EXECUTABLE "/usr/bin/systemctl" CACHE FILEPATH "systemctl, run from the udev rule")
set(UDEVADM_EXECUTABLE "/usr/bin/udevadm" CACHE FILEPATH "udevadm")

# the real action file lives outside plasma/kickeractions. Plasma only sees it
# through the symlink chain kickeractions -> /run -> this file, and the /run
# link only exists on hybrid systems.
set(DGPU_ACTION_FILE "${KDE_INSTALL_FULL_DATADIR}/ultramarine/ultramarine-dgpu-launch.desktop")
set(DGPU_RUNTIME_LINK "/run/ultramarine-dgpu-launch/ultramarine-dgpu-launch.desktop")

# picks the discrete GPU's environment, shared with the tests
add_library(ultramarine_dgpu_environment STATIC gpuenvironment.cpp)
target_include_directories(ultramarine_dgpu_environment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ultramarine_dgpu_environment PUBLIC Qt6::Core Qt6::DBus)

# runs the app with that environment, see main.cpp for why not kioclient or switcherooctl launch
add_executable(ultramarine-dgpu-launch main.cpp)
target_compile_definitions(ultramarine-dgpu-launch PRIVATE TRANSLATION_DOMAIN="ultramarine_dgpu_launch")
target_link_libraries(ultramarine-dgpu-launch
        PRIVATE
        ultramarine_dgpu_environment
        Qt6::Widgets
        KF6::I18n
        KF6::Service
        KF6::KIOCore
        KF6::KIOGui
        KF6::WidgetsAddons
)
cmake_path(GET DGPU_LAUNCH_EXECUTABLE PARENT_PATH _launch_dir)
install(TARGETS ultramarine-dgpu-launch RUNTIME DESTINATION ${_launch_dir} ${_component})

set(_data ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(_generated ${CMAKE_CURRENT_BINARY_DIR}/data)
configure_file(ultramarine-dgpu-detect.in ${_generated}/ultramarine-dgpu-detect @ONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
configure_file(${_data}/90-ultramarine-dgpu.rules.in ${_generated}/90-ultramarine-dgpu.rules @ONLY)
configure_file(${_data}/ultramarine-dgpu-detect.service.in ${_generated}/ultramarine-dgpu-detect.service @ONLY)
configure_file(${_data}/ultramarine-dgpu-launch.desktop.in ${_generated}/ultramarine-dgpu-launch.desktop @ONLY)

cmake_path(GET DGPU_DETECT_EXECUTABLE PARENT_PATH _detect_dir)
install(PROGRAMS ${_generated}/ultramarine-dgpu-detect DESTINATION ${_detect_dir} ${_component})
install(FILES ${_generated}/90-ultramarine-dgpu.rules DESTINATION ${UDEV_RULES_DIR} ${_component})
install(FILES ${_generated}/ultramarine-dgpu-detect.service DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR} ${_component})
install(FILES ${_generated}/ultramarine-dgpu-launch.desktop DESTINATION ${KDE_INSTALL_DATADIR}/ultramarine ${_component})

# dangling until ultramarine-dgpu-detect creates the /run link. Plasma lists
# kickeractions with QDir::Files, which skips broken symlinks.
file(MAKE_DIRECTORY ${_generated}/kickeractions)
file(CREATE_LINK ${DGPU_RUNTIME_LINK} ${_generated}/kickeractions/ultramarine-dgpu-launch.desktop SYMBOLIC)
install(FILES ${_generated}/kickeractions/ultramarine-dgpu-launch.desktop
        DESTINATION ${KDE_INSTALL_DATADIR}/plasma/kickeractions ${_component})
