# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

kcmutils_add_qml_kcm(kcm_ultramarine_tweaks
        SOURCES
        tweaks.cpp
        DISABLE_DESKTOP_FILE_GENERATION
)
um_translate_kcm_metadata(kcm_ultramarine_tweaks)

target_compile_definitions(kcm_ultramarine_tweaks PRIVATE TRANSLATION_DOMAIN="kcm_ultramarine_tweaks")

target_link_libraries(kcm_ultramarine_tweaks
        PRIVATE
        kcm_common
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        KF6::CoreAddons
        KF6::I18n
        KF6::KCMUtilsQuick
)
