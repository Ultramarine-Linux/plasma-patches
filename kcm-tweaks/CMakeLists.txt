# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# Named kcm_ultramarine_* to avoid colliding with Discover's kcm_updates plugin.
# kcmutils_add_qml_kcm installs the plugin into plasma/kcms/systemsettings.
kcmutils_add_qml_kcm(kcm_ultramarine_tweaks
        SOURCES
        tweaks.cpp
)

target_compile_definitions(kcm_ultramarine_tweaks PRIVATE TRANSLATION_DOMAIN="kcm_ultramarine_tweaks")

target_link_libraries(kcm_ultramarine_tweaks
        PRIVATE
        umcli_common
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtilsQuick
)
