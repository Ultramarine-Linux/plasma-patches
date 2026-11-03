# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# a stand-in until Plasma ships its own location KCM (plasma-workspace!5434 only sets a manual
# position, it has no on/off switch yet)
kcmutils_add_qml_kcm(kcm_ultramarine_location
        SOURCES
        location.cpp
)

# the root helper that masks or unmasks geoclue.service, run through pkexec. Its polkit
# action matches this exact path. Like the paths in the top-level CMakeLists.txt, it
# follows the install prefix unless set with -D.
if (NOT DEFINED ULTRAMARINE_LOCATION_HELPER)
    set(ULTRAMARINE_LOCATION_HELPER "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-location-helper")
endif ()

target_compile_definitions(kcm_ultramarine_location PRIVATE
        TRANSLATION_DOMAIN="kcm_ultramarine_location"
        ULTRAMARINE_LOCATION_HELPER="${ULTRAMARINE_LOCATION_HELPER}")

target_link_libraries(kcm_ultramarine_location
        PRIVATE
        kcm_common
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtilsQuick
)

set(_data ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(_generated ${CMAKE_CURRENT_BINARY_DIR}/data)
configure_file(${_data}/ultramarine-location-helper.in ${_generated}/ultramarine-location-helper @ONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
configure_file(${_data}/org.ultramarine.location.policy.in ${_generated}/org.ultramarine.location.policy @ONLY)

cmake_path(GET ULTRAMARINE_LOCATION_HELPER PARENT_PATH _helper_dir)
install(PROGRAMS ${_generated}/ultramarine-location-helper DESTINATION ${_helper_dir})
# polkit only reads actions from /usr/share/polkit-1/actions
install(FILES ${_generated}/org.ultramarine.location.policy
        DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions)
