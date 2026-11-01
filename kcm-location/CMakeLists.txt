# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# a stand-in until Plasma ships its own location KCM (plasma-workspace!5434 only sets a manual
# position, it has no on/off switch yet)
kcmutils_add_qml_kcm(kcm_ultramarine_location
        SOURCES
        location.cpp
)

target_compile_definitions(kcm_ultramarine_location PRIVATE TRANSLATION_DOMAIN="kcm_ultramarine_location")

target_link_libraries(kcm_ultramarine_location
        PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Qml
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtilsQuick
)
