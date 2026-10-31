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
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_kcm_test(test_updates test_updates.cpp kcm-updates updates.cpp)

add_kcm_test(test_tweaks test_tweaks.cpp kcm-tweaks tweaks.cpp)

add_executable(test_updater test_updater.cpp)
target_link_libraries(test_updater PRIVATE ultramarine_updates_core Qt6::Test)
add_test(NAME test_updater COMMAND test_updater)

add_test(NAME test_dgpu_detect
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_dgpu_detect.sh ${CMAKE_BINARY_DIR}/dgpu-launch/data/ultramarine-dgpu-detect)
