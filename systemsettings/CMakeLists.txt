# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

set(_category settings-ultramarine.desktop)
um_translate_desktop_file(${_category}.in ${CMAKE_CURRENT_BINARY_DIR}/${_category})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_category} DESTINATION ${KDE_INSTALL_DATADIR}/systemsettings/categories)
