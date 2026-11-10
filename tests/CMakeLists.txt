# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

function(add_kcm_test name source kcm_dir kcm_source)
    add_executable(${name} ${source} ../${kcm_dir}/${kcm_source})
    target_include_directories(${name} PRIVATE ../${kcm_dir})
    target_link_libraries(${name}
        PRIVATE
            kcm_common
            Qt6::Core
            Qt6::DBus
            Qt6::Gui
            Qt6::Qml
            Qt6::Test
            KF6::ConfigCore
            KF6::CoreAddons
            KF6::I18n
            KF6::KCMUtilsQuick
    )
    # moc embeds the KCM's generated JSON metadata
    string(REPLACE "kcm-" "kcm_ultramarine_" kcm_target ${kcm_dir})
    target_include_directories(${name} PRIVATE ${CMAKE_BINARY_DIR}/${kcm_dir})
    um_depend_on_kcm_metadata(${name} ${kcm_target})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_kcm_test(test_updates test_updates.cpp kcm-updates updates.cpp)
# checks what the KCM sends against the helper's own parsing
target_link_libraries(test_updates PRIVATE ultramarine_updates_core)

add_kcm_test(test_tweaks test_tweaks.cpp kcm-tweaks tweaks.cpp)
# runs a fake um rather than whatever is installed, see umcli.h. It's copied with the
# executable bit set, since UmCli::executable() needs it and git may not keep the mode.
configure_file(fake_um.sh ${CMAKE_CURRENT_BINARY_DIR}/fake_um.sh COPYONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
target_compile_definitions(test_tweaks PRIVATE UM_TEST_EXECUTABLE="${CMAKE_CURRENT_BINARY_DIR}/fake_um.sh")

add_kcm_test(test_location test_location.cpp kcm-location location.cpp)

add_executable(test_updater test_updater.cpp)
target_link_libraries(test_updater PRIVATE ultramarine_updates_core Qt6::Test)
add_test(NAME test_updater COMMAND test_updater)

add_executable(test_office_installer test_office_installer.cpp)
target_link_libraries(test_office_installer PRIVATE ultramarine_office_suites Qt6::Test)
add_test(NAME test_office_installer COMMAND test_office_installer)

add_executable(test_dgpu_launch test_dgpu_launch.cpp)
target_link_libraries(test_dgpu_launch PRIVATE ultramarine_dgpu_environment Qt6::Test)
add_test(NAME test_dgpu_launch COMMAND test_dgpu_launch)

add_test(NAME test_dgpu_detect
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_dgpu_detect.sh ${CMAKE_BINARY_DIR}/dgpu-launch/data/ultramarine-dgpu-detect)
