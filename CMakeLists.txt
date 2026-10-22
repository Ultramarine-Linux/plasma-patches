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

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake )

include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED Core Quick Gui Qml Test)

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Config
    CoreAddons
    KCMUtils
    I18n
)

include(CTest)

# Absolute path of the um CLI. umcli's polkit action (org.ultramarine.um.manage)
# matches this exact path when um escalates through pkexec.
set(UM_EXECUTABLE "/usr/bin/um" CACHE FILEPATH "Path to the um CLI")

add_library(umcli_common INTERFACE)
target_include_directories(umcli_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_definitions(umcli_common INTERFACE UM_EXECUTABLE="${UM_EXECUTABLE}")


add_subdirectory(kcm-tweaks)
add_subdirectory(kcm-updates)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
