# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# the update backend: listing, installing, automatic updates and Apply Tonight.
# The library is shared by the helper and the tests.
add_library(ultramarine_updates_core STATIC
        cli.cpp
        process.cpp
        updates.cpp
        apps.cpp
        rpm.cpp
        flatpak.cpp
        nix.cpp
        image.cpp
        packagekit.cpp
        plasmoid.cpp
        settings.cpp
        activity.cpp
        lock.cpp
        inhibit.cpp
        schedule.cpp
)

target_include_directories(ultramarine_updates_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ultramarine_updates_core PRIVATE TRANSLATION_DOMAIN="${UM_TRANSLATION_DOMAIN}")
target_link_libraries(ultramarine_updates_core
        PUBLIC
        Qt6::Core
        KF6::I18n
        PRIVATE
        Qt6::DBus
        KF6::NewStuffCore
)

add_executable(ultramarine-updates main.cpp)
target_compile_definitions(ultramarine-updates PRIVATE TRANSLATION_DOMAIN="${UM_TRANSLATION_DOMAIN}")
target_link_libraries(ultramarine-updates PRIVATE ultramarine_updates_core)

# everything the helper needs at runtime is in this component, so a development
# build can install just the backend: cmake --install build --component ultramarine-updates
set(_component COMPONENT ultramarine-updates)

cmake_path(GET ULTRAMARINE_UPDATES_EXECUTABLE PARENT_PATH _helper_dir)
install(TARGETS ultramarine-updates RUNTIME DESTINATION ${_helper_dir} ${_component})

set(_data ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(_generated ${CMAKE_CURRENT_BINARY_DIR}/data)
foreach (_file
        ultramarine-updates-automatic.service
        ultramarine-updates-user.service)
    configure_file(${_data}/${_file}.in ${_generated}/${_file} @ONLY)
endforeach ()
um_translate_policy(${_data}/org.ultramarine.updates.policy.in ${_generated}/org.ultramarine.updates.policy)
um_translate_desktop_file(${_data}/ultramarine-updates-user.desktop.in ${_generated}/ultramarine-updates-user.desktop)

# polkit only reads actions from /usr/share/polkit-1/actions
install(FILES ${_generated}/org.ultramarine.updates.policy
        DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions ${_component})

# the system timer has an [Install] section, and Ultramarine's presets enable it
install(FILES ${_generated}/ultramarine-updates-automatic.service ${_data}/ultramarine-updates-automatic.timer
        DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR} ${_component})

install(FILES ${_generated}/ultramarine-updates-user.service ${_data}/ultramarine-updates-user.timer
        DESTINATION ${SYSTEMD_USER_UNIT_DIR} ${_component})
install(FILES ${_generated}/ultramarine-updates-user.desktop
        DESTINATION ${KDE_INSTALL_AUTOSTARTDIR} ${_component})
