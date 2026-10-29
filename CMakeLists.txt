# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.16)

set(QT_MIN_VERSION "6.5.0")
set(KF6_MIN_VERSION "6.0.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(kcm_ultramarine)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED Core Quick Gui Qml Test Widgets)

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
        Config
        CoreAddons
        KCMUtils
        I18n
        KIO
        Service
        WidgetsAddons
)

include(CTest)

# Compile Weblate-maintained gettext catalogs (po/<locale>/<domain>.po).
# Don't install an empty share/locale tree before translations exist; CONFIGURE_DEPENDS
# re-runs CMake when Weblate adds the first PO catalog.
file(GLOB_RECURSE KCM_TRANSLATION_CATALOGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/*/*.po")
if(KCM_TRANSLATION_CATALOGS)
    ki18n_install(po)
endif()

# Absolute path of the um CLI, used by the Tweaks KCM. umcli's polkit action
# (org.ultramarine.um.manage) matches this exact path when um escalates through pkexec.
set(UM_EXECUTABLE "/usr/bin/um" CACHE FILEPATH "Path to the um CLI")

# Absolute path of the update helper (updater/), used by the Updates KCM. Its polkit
# action (org.ultramarine.updates.manage) matches this exact path.
set(ULTRAMARINE_UPDATES_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-updates"
    CACHE FILEPATH "Path the update helper is installed to")
set(SYSTEMD_SYSTEM_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system" CACHE PATH "systemd system units")
set(SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user" CACHE PATH "systemd user units")

add_library(kcm_common INTERFACE)
target_include_directories(kcm_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_definitions(kcm_common INTERFACE
    UM_EXECUTABLE="${UM_EXECUTABLE}"
    ULTRAMARINE_UPDATES_EXECUTABLE="${ULTRAMARINE_UPDATES_EXECUTABLE}")


add_subdirectory(updater)
add_subdirectory(kcm-tweaks)
add_subdirectory(kcm-updates)
add_subdirectory(dgpu-launch)
add_subdirectory(office-installer)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
