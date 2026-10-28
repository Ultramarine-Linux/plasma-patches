# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# Offers to install an office suite when an office document is opened and none
# is installed. See docs/office-installer.md.
set(_component COMPONENT ultramarine-office-installer)

set(OFFICE_INSTALLER_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-office-installer"
        CACHE FILEPATH "Path the office suite installer is installed to")

# The suite list is shared with the tests.
add_library(ultramarine_office_suites STATIC suites.cpp)
target_include_directories(ultramarine_office_suites PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ultramarine_office_suites PRIVATE TRANSLATION_DOMAIN="ultramarine_office_installer")
target_link_libraries(ultramarine_office_suites PUBLIC Qt6::Core KF6::I18n)

add_executable(ultramarine-office-installer main.cpp installerdialog.cpp launch.cpp)
target_compile_definitions(ultramarine-office-installer PRIVATE TRANSLATION_DOMAIN="ultramarine_office_installer")
target_link_libraries(ultramarine-office-installer
        PRIVATE
        ultramarine_office_suites
        Qt6::Widgets
        KF6::I18n
        KF6::Service
        KF6::KIOGui
        KF6::WidgetsAddons
)

cmake_path(GET OFFICE_INSTALLER_EXECUTABLE PARENT_PATH _installer_dir)
install(TARGETS ultramarine-office-installer RUNTIME DESTINATION ${_installer_dir} ${_component})

set(_desktop org.ultramarine.OfficeInstaller.desktop)
configure_file(data/${_desktop}.in ${CMAKE_CURRENT_BINARY_DIR}/data/${_desktop} @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/${_desktop} DESTINATION ${KDE_INSTALL_APPDIR} ${_component})
