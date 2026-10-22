# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

add_executable(test_updates
    test_updates.cpp
    ../kcm-updates/updates.cpp
)

target_include_directories(test_updates PRIVATE ../kcm-updates)

target_link_libraries(test_updates
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Test
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtilsQuick
)

add_test(NAME test_updates COMMAND test_updates)
