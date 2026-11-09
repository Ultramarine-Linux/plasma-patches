# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.29)

# 6.6 for QFileInfo::readSymLink() and QProcess::setUnixProcessParameters() in updater/
set(QT_MIN_VERSION "6.6.0")
set(KF6_MIN_VERSION "6.0.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(kcm_ultramarine)

# link with lld when it's installed, unless -DCMAKE_LINKER_TYPE picks a linker
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

# the Weblate catalog (po/<lang>.po), see docs/translations.md. The KCMs install it as
# their own domains, the update helper, dGPU launcher and office installer share this one.
include(UltramarineTranslations)
um_install_translations(${UM_TRANSLATION_DOMAIN}
        COMPONENTS ultramarine-updates ultramarine-dgpu-launch ultramarine-office-installer)

# absolute path of the um CLI, used by the Tweaks KCM. umcli's polkit action
# (org.ultramarine.um.tweaks) matches this exact path plus the `tweaks` subcommand
# (exec.argv1) when um escalates through pkexec.
set(UM_EXECUTABLE "/usr/bin/um" CACHE FILEPATH "Path to the um CLI")

# absolute path of the update helper (updater/), used by the Updates KCM. Its polkit
# actions match this exact path plus the subcommand (exec.argv1).
# These follow the install prefix unless set with -D. They aren't cache defaults, so
# changing CMAKE_INSTALL_PREFIX in an existing build directory doesn't leave stale paths
# (build directories configured before they followed the prefix still have them cached, see docs/building.md).
if (NOT DEFINED ULTRAMARINE_UPDATES_EXECUTABLE)
    set(ULTRAMARINE_UPDATES_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/libexec/ultramarine-updates")
endif ()
if (NOT DEFINED SYSTEMD_SYSTEM_UNIT_DIR)
    set(SYSTEMD_SYSTEM_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system")
endif ()
if (NOT DEFINED SYSTEMD_USER_UNIT_DIR)
    set(SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user")
endif ()

add_library(kcm_common INTERFACE)
target_include_directories(kcm_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_definitions(kcm_common INTERFACE
    UM_EXECUTABLE="${UM_EXECUTABLE}"
    ULTRAMARINE_UPDATES_EXECUTABLE="${ULTRAMARINE_UPDATES_EXECUTABLE}")


add_subdirectory(updater)
add_subdirectory(kcm-tweaks)
add_subdirectory(kcm-updates)
add_subdirectory(kcm-location)
add_subdirectory(dgpu-launch)
add_subdirectory(office-installer)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
