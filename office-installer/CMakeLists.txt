# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# offers to install an office suite when an office document is opened and none
# is installed. See docs/office-installer.md.
set(_component COMPONENT ultramarine-office-installer)

# like the paths in the top-level CMakeLists.txt, this follows the install prefix unless set with -D
if (NOT DEFINED OFFICE_INSTALLER_EXECUTABLE)
    set(OFFICE_INSTALLER_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-office-installer")
endif ()

# installs Flatpaks with libflatpak: the flatpak CLI can't install without a terminal
# and still let polkit ask for a password
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLATPAK REQUIRED IMPORTED_TARGET flatpak)

# the suite list and the PackageKit installer are shared with the tests
add_library(ultramarine_office_suites STATIC suites.cpp packagekit.cpp installer.h)
target_include_directories(ultramarine_office_suites PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ultramarine_office_suites PRIVATE TRANSLATION_DOMAIN="ultramarine_office_installer")
target_link_libraries(ultramarine_office_suites PUBLIC Qt6::Core Qt6::DBus KF6::I18n)

add_executable(ultramarine-office-installer main.cpp installerdialog.cpp launch.cpp flatpakinstaller.cpp)
target_compile_definitions(ultramarine-office-installer PRIVATE TRANSLATION_DOMAIN="ultramarine_office_installer")
target_link_libraries(ultramarine-office-installer
        PRIVATE
        ultramarine_office_suites
        Qt6::DBus
        Qt6::Widgets
        KF6::I18n
        KF6::Service
        KF6::KIOCore
        KF6::KIOGui
        KF6::KIOWidgets
        KF6::WidgetsAddons
        PkgConfig::FLATPAK
)

cmake_path(GET OFFICE_INSTALLER_EXECUTABLE PARENT_PATH _installer_dir)
install(TARGETS ultramarine-office-installer RUNTIME DESTINATION ${_installer_dir} ${_component})

set(_desktop org.ultramarine.OfficeInstaller.desktop)
configure_file(data/${_desktop}.in ${CMAKE_CURRENT_BINARY_DIR}/data/${_desktop} @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/data/${_desktop} DESTINATION ${KDE_INSTALL_APPDIR} ${_component})
