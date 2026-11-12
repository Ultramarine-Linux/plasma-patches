# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.29)

# updater/ needs QFileInfo::readSymLink() and QProcess::setUnixProcessParameters() from 6.6
set(QT_MIN_VERSION "6.6.0")
set(KF6_MIN_VERSION "6.0.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(kcm_ultramarine)

if (NOT DEFINED CMAKE_LINKER_TYPE)
    find_program(LLD_EXECUTABLE NAMES ld.lld lld)
    if (LLD_EXECUTABLE)
        set(CMAKE_LINKER_TYPE LLD)
    endif ()
endif ()

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED Core DBus Quick Gui Qml Test Widgets)

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
        Config
        CoreAddons
        KCMUtils
        I18n
        KIO
        Service
        WidgetsAddons
        NewStuffCore
)

include(CTest)

include(UltramarineTranslations)
um_install_translations(${UM_TRANSLATION_DOMAIN}
        COMPONENTS ultramarine-updates ultramarine-dgpu-launch ultramarine-office-installer)

# polkit actions match the exact executable paths below
set(UM_EXECUTABLE "/usr/bin/um" CACHE FILEPATH "Path to the um CLI")

# not cache defaults, so changing CMAKE_INSTALL_PREFIX in an existing build directory doesn't leave stale paths
if (NOT DEFINED ULTRAMARINE_UPDATES_EXECUTABLE)
    set(ULTRAMARINE_UPDATES_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-updates")
endif ()

find_package(PkgConfig QUIET)
foreach (_unit_dir SYSTEM USER)
    if (DEFINED SYSTEMD_${_unit_dir}_UNIT_DIR)
        continue()
    endif ()
    string(TOLOWER "${_unit_dir}" _kind)
    set(_pc_dir "")
    if (PKG_CONFIG_FOUND)
        pkg_get_variable(_pc_dir systemd systemd${_kind}unitdir DEFINE_VARIABLES prefix=${CMAKE_INSTALL_PREFIX})
    endif ()
    if (_pc_dir)
        set(SYSTEMD_${_unit_dir}_UNIT_DIR "${_pc_dir}")
    else ()
        set(SYSTEMD_${_unit_dir}_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/${_kind}")
    endif ()
endforeach ()

# udev.pc has no prefix variable to override, so swap /usr for our prefix by hand
if (NOT DEFINED UDEV_RULES_DIR)
    set(_pc_dir "")
    if (PKG_CONFIG_FOUND)
        pkg_get_variable(_pc_dir udev udevdir)
    endif ()
    if (_pc_dir MATCHES "^/usr(/.+)$")
        set(UDEV_RULES_DIR "${CMAKE_INSTALL_PREFIX}${CMAKE_MATCH_1}/rules.d")
    else ()
        set(UDEV_RULES_DIR "${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d")
    endif ()
endif ()

add_library(kcm_common INTERFACE)
target_include_directories(kcm_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_definitions(kcm_common INTERFACE
    UM_EXECUTABLE="${UM_EXECUTABLE}"
    ULTRAMARINE_UPDATES_EXECUTABLE="${ULTRAMARINE_UPDATES_EXECUTABLE}")

add_subdirectory(systemsettings)
add_subdirectory(updater)
add_subdirectory(kcm-tweaks)
add_subdirectory(kcm-updates)
add_subdirectory(kcm-location)
add_subdirectory(dgpu-launch)
add_subdirectory(office-installer)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
