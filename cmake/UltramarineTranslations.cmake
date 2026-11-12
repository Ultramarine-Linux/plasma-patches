# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

find_program(MSGFMT_EXECUTABLE msgfmt REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# msgfmt --xml finds these itself, but without a clear error when they're missing
find_file(POLKIT_ITS_RULES polkit.its
        PATHS ${CMAKE_INSTALL_PREFIX}/share /usr/local/share /usr/share ENV XDG_DATA_DIRS
        PATH_SUFFIXES gettext/its
        REQUIRED
        DOC "polkit's gettext ITS rules (polkit-devel)")

set(UM_TRANSLATION_DOMAIN ultramarine-plasma-settings)
# msgfmt reads LINGUAS next to the catalogs, so copying them here avoids keeping LINGUAS in po/
set(UM_PO_DIR ${CMAKE_BINARY_DIR}/po)
set(_kcm_json_l10n ${CMAKE_CURRENT_LIST_DIR}/kcm-json-l10n.py)

file(GLOB _catalogs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/po/*.po)
set(UM_LANGUAGES)
set(UM_PO_FILES)
set(UM_MO_FILES)
foreach (_po ${_catalogs})
    cmake_path(GET _po STEM _lang)
    list(APPEND UM_LANGUAGES ${_lang})
    add_custom_command(OUTPUT ${UM_PO_DIR}/${_lang}.po
            COMMAND ${CMAKE_COMMAND} -E copy ${_po} ${UM_PO_DIR}/${_lang}.po
            DEPENDS ${_po}
            VERBATIM)
    add_custom_command(OUTPUT ${UM_PO_DIR}/${_lang}.mo
            COMMAND ${MSGFMT_EXECUTABLE} --check -o ${UM_PO_DIR}/${_lang}.mo ${UM_PO_DIR}/${_lang}.po
            DEPENDS ${UM_PO_DIR}/${_lang}.po
            VERBATIM)
    list(APPEND UM_PO_FILES ${UM_PO_DIR}/${_lang}.po)
    list(APPEND UM_MO_FILES ${UM_PO_DIR}/${_lang}.mo)
endforeach ()
list(JOIN UM_LANGUAGES "\n" _linguas)
file(CONFIGURE OUTPUT ${UM_PO_DIR}/LINGUAS CONTENT "${_linguas}\n" @ONLY)

add_custom_target(um_translations ALL DEPENDS ${UM_PO_FILES} ${UM_MO_FILES})

# a shared domain is installed once per component so cmake --install --component works alone
function(um_install_translations domain)
    cmake_parse_arguments(ARG "" "" "COMPONENTS" ${ARGN})
    set(_components ${ARG_COMPONENTS})
    if (NOT _components)
        set(_components Unspecified)
    endif ()
    foreach (_component IN LISTS _components)
        set(_component_args COMPONENT ${_component})
        foreach (_lang ${UM_LANGUAGES})
            install(FILES ${UM_PO_DIR}/${_lang}.mo
                    DESTINATION ${KDE_INSTALL_LOCALEDIR}/${_lang}/LC_MESSAGES
                    RENAME ${domain}.mo
                    ${_component_args})
        endforeach ()
    endforeach ()
endfunction()

# msgfmt --xml only finds polkit's ITS rules when the template's name ends in .policy
function(_um_msgfmt_merge mode template output)
    cmake_path(GET output FILENAME _name)
    # Ninja creates output directories, Make doesn't
    cmake_path(GET output PARENT_PATH _dir)
    file(MAKE_DIRECTORY ${_dir})
    add_custom_command(OUTPUT ${output}
            COMMAND ${MSGFMT_EXECUTABLE} ${mode} --template=${template} -d ${UM_PO_DIR} -o ${output}
            DEPENDS ${template} ${UM_PO_FILES} ${UM_PO_DIR}/LINGUAS
            VERBATIM)
    string(MAKE_C_IDENTIFIER "um_l10n_${_name}" _target)
    add_custom_target(${_target} ALL DEPENDS ${output})
    # the catalogs are built in the top-level directory, which only Ninja tracks per file
    add_dependencies(${_target} um_translations)
endfunction()

function(um_translate_desktop_file input output)
    cmake_path(GET output FILENAME _name)
    set(_template ${CMAKE_CURRENT_BINARY_DIR}/untranslated/${_name})
    configure_file(${input} ${_template} @ONLY)
    _um_msgfmt_merge(--desktop ${_template} ${output})
endfunction()

function(um_translate_policy input output)
    cmake_path(GET output FILENAME _name)
    set(_template ${CMAKE_CURRENT_BINARY_DIR}/untranslated/${_name})
    configure_file(${input} ${_template} @ONLY)
    _um_msgfmt_merge(--xml ${_template} ${output})
endfunction()

# source file properties are per directory, so call this from the directory that creates <target>
function(um_depend_on_kcm_metadata target kcm_target)
    get_target_property(_kcm_binary_dir ${kcm_target} BINARY_DIR)
    get_target_property(_sources ${target} SOURCES)
    list(FILTER _sources INCLUDE REGEX "\\.cpp$")
    set_property(SOURCE ${_sources} APPEND PROPERTY OBJECT_DEPENDS ${_kcm_binary_dir}/${kcm_target}.json)
    set_property(TARGET ${target} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS ${kcm_target}_metadata)
endfunction()

# generates the KCM's desktop file too, so pass DISABLE_DESKTOP_FILE_GENERATION to kcmutils_add_qml_kcm
function(um_translate_kcm_metadata target)
    set(_input ${CMAKE_CURRENT_SOURCE_DIR}/${target}.json.in)
    set(_json ${CMAKE_CURRENT_BINARY_DIR}/${target}.json)
    set(_desktop ${CMAKE_CURRENT_BINARY_DIR}/${target}.desktop)

    add_custom_command(OUTPUT ${_json}
            COMMAND Python3::Interpreter -I ${_kcm_json_l10n} merge ${_input} ${_json} ${UM_PO_DIR}
            DEPENDS ${_input} ${_kcm_json_l10n} ${UM_MO_FILES} ${UM_PO_DIR}/LINGUAS
            VERBATIM)
    add_custom_command(OUTPUT ${_desktop}
            COMMAND KF6::kcmdesktopfilegenerator ${_json} ${_desktop}
            DEPENDS ${_json}
            VERBATIM)
    add_custom_target(${target}_metadata DEPENDS ${_json} ${_desktop})
    add_dependencies(${target}_metadata um_translations)

    add_dependencies(${target} ${target}_metadata)
    set_property(TARGET ${target} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS ${target}_metadata)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    # Ninja decides the source including the .moc is clean before autogen rewrites the .moc
    um_depend_on_kcm_metadata(${target} ${target})

    install(FILES ${_desktop} DESTINATION ${KDE_INSTALL_APPDIR})
    um_install_translations(${target})
endfunction()
