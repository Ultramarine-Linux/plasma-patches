# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

kcmutils_add_qml_kcm(kcm_tweaks
    SOURCES
        tweaks.cpp
)

target_link_libraries(kcm_tweaks
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtilsQuick
)

install(TARGETS kcm_tweaks DESTINATION ${KDE_INSTALL_PLUGINDIR})
