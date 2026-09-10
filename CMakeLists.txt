# **********************************************************************
# * Copyright (C) 2017-2026 MX Authors
# *
# * Authors: Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of custom-toolbox.
# *
# * custom-toolbox is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * custom-toolbox is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with custom-toolbox.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VERSION_FROM_CHANGELOG "")

if(DEFINED PROJECT_VERSION_OVERRIDE AND NOT "${PROJECT_VERSION_OVERRIDE}" STREQUAL "")
    set(PROJECT_VERSION_FROM_CHANGELOG "${PROJECT_VERSION_OVERRIDE}")
    message(STATUS "Using version override: ${PROJECT_VERSION_FROM_CHANGELOG}")
else()
    # Try dpkg-parsechangelog first (Debian/Ubuntu)
    execute_process(
        COMMAND dpkg-parsechangelog -SVersion
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_VERSION_FROM_CHANGELOG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE DPKG_RESULT
        ERROR_QUIET
    )

    if(DPKG_RESULT EQUAL 0 AND NOT "${PROJECT_VERSION_FROM_CHANGELOG}" STREQUAL "")
        message(STATUS "Using version from debian/changelog: ${PROJECT_VERSION_FROM_CHANGELOG}")
    elseif(EXISTS ${CMAKE_SOURCE_DIR}/debian/changelog)
        file(READ ${CMAKE_SOURCE_DIR}/debian/changelog PROJECT_CHANGELOG_CONTENT)
        string(REGEX MATCH "^[^\\(]*\\(([^)]+)\\)" _match "${PROJECT_CHANGELOG_CONTENT}")
        if(_match)
            set(PROJECT_VERSION_FROM_CHANGELOG "${CMAKE_MATCH_1}")
            message(STATUS "Using version parsed from debian/changelog: ${PROJECT_VERSION_FROM_CHANGELOG}")
        else()
            message(WARNING "Could not parse version from debian/changelog, defaulting to 0.0.0")
            set(PROJECT_VERSION_FROM_CHANGELOG "0.0.0")
        endif()
    else()
        message(WARNING "Could not determine version automatically, defaulting to 0.0.0")
        set(PROJECT_VERSION_FROM_CHANGELOG "0.0.0")
    endif()
endif()

# Option to use clang for testing builds - needs to be early so project() picks it up
option(USE_CLANG "Use clang compiler" OFF)
if(USE_CLANG)
    set(CMAKE_C_COMPILER clang CACHE STRING "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER clang++ CACHE STRING "C++ compiler" FORCE)
    message(STATUS "Using clang compiler")
endif()

project(custom-toolbox
    VERSION ${PROJECT_VERSION_FROM_CHANGELOG}
    DESCRIPTION "Custom Toolbox - Program for creating custom launchers for MX Linux"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands export for IDEs and tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimize for Ninja builds
if(CMAKE_GENERATOR STREQUAL "Ninja")
    # Enable colored output for Ninja
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    LinguistTools
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

# Enable automatic MOC, UIC, and RCC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)
# These policies were introduced after the minimum Qt version available on
# some supported distributions. Only set them when the installed Qt knows
# about them.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt6_policy(SET QTP0001 NEW)
endif()
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt6_policy(SET QTP0004 NEW)
endif()

# Define source files
set(SOURCES
    src/main.cpp
    src/launchermodel.cpp
    src/launcherparser.cpp
    src/iconloader.cpp
)

set(HEADERS
    src/launchermodel.h
    src/common.h
    src/iteminfo.h
    src/launcherparser.h
    src/iconloader.h
)

# Get all translation files
file(GLOB TRANSLATION_FILES "translations/*.ts")
list(FILTER TRANSLATION_FILES EXCLUDE REGEX "/custom-toolbox_en_US\\.ts$")

# Create the executable
add_executable(custom-toolbox
    ${SOURCES}
    ${HEADERS}
)

# qml/Main.qml uses Qt.labs.settings so it keeps working on distributions
# with Qt < 6.5 (e.g. Debian Bookworm). On Qt >= 6.5, generate a build copy
# with the import switched to the non-deprecated QtCore Settings type
# instead, which has the same properties this file relies on.
set(MAIN_QML_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MAIN_QML_SOURCE}")
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    set(MAIN_QML_ENTRY "${CMAKE_CURRENT_BINARY_DIR}/generated-qml/Main.qml")
    file(READ "${MAIN_QML_SOURCE}" MAIN_QML_CONTENT)
    string(REPLACE "import Qt.labs.settings" "import QtCore" MAIN_QML_CONTENT "${MAIN_QML_CONTENT}")
    file(GENERATE OUTPUT "${MAIN_QML_ENTRY}" CONTENT "${MAIN_QML_CONTENT}")
    set_source_files_properties("${MAIN_QML_ENTRY}" PROPERTIES QT_RESOURCE_ALIAS "Main.qml")
else()
    set(MAIN_QML_ENTRY "${MAIN_QML_SOURCE}")
endif()

qt6_add_qml_module(custom-toolbox
    URI CustomToolbox
    VERSION 1.0
    QML_FILES
        ${MAIN_QML_ENTRY}
        qml/components/CategoryButton.qml
        qml/components/LauncherCard.qml
        qml/components/ModernSwitch.qml
        qml/components/SecondaryButton.qml
        qml/components/ThemeToolTip.qml
    RESOURCES
        icons/custom-toolbox.svg
)

# Link Qt6 libraries
target_link_libraries(custom-toolbox PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

# Set compiler flags
target_compile_options(custom-toolbox PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
    -Werror
)

# Add compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(custom-toolbox PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(custom-toolbox PRIVATE -Werror=return-local-addr)
endif()

# Set compile definitions
target_compile_definitions(custom-toolbox PRIVATE
    QT_DEPRECATED_WARNINGS
    VERSION="${PROJECT_VERSION}"
    CUSTOM_TOOLBOX_SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}"
    CUSTOM_TOOLBOX_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}"
)

# Release-specific optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(custom-toolbox PRIVATE NDEBUG)
    target_compile_options(custom-toolbox PRIVATE -Os)

    # Add LTO - different flags for different compilers
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(custom-toolbox PRIVATE -flto=thin)
        target_link_options(custom-toolbox PRIVATE -flto=thin)
    else()
        target_compile_options(custom-toolbox PRIVATE -flto=auto)
        target_link_options(custom-toolbox PRIVATE -flto=auto)
    endif()
endif()

# Handle translations
qt6_add_translations(custom-toolbox
    TS_FILES ${TRANSLATION_FILES}
    LRELEASE_OPTIONS -compress -nounfinished -removeidentical -silent
    QM_FILES_OUTPUT_VARIABLE qm_files
)

# Set target properties
set_target_properties(custom-toolbox PROPERTIES
    OUTPUT_NAME "custom-toolbox"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Install target (required by Debian build system)
# Other files are handled by debian/install
install(TARGETS custom-toolbox
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${qm_files}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/custom-toolbox/locale
)

install(FILES
    custom-toolbox.conf
    example.list
    example-ini.list
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/custom-toolbox
)

install(FILES custom-toolbox.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES icons/custom-toolbox.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
)

install(FILES icons/custom-toolbox.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

install(DIRECTORY help/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/custom-toolbox
    PATTERN "*.1" EXCLUDE
)

install(FILES help/custom-toolbox.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install(FILES debian/changelog
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/custom-toolbox
)

if(BUILD_TESTING)
    add_executable(test-launcherparser
        tests/test_launcherparser.cpp
        src/launcherparser.cpp
        src/launcherparser.h
    )
    target_link_libraries(test-launcherparser PRIVATE Qt6::Core Qt6::Test)
    target_compile_options(test-launcherparser PRIVATE
        -Wpedantic
        -pedantic
        -Werror=return-type
        -Werror=switch
        -Werror=uninitialized
        -Werror
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(test-launcherparser PRIVATE -Werror=return-stack-address)
    else()
        target_compile_options(test-launcherparser PRIVATE -Werror=return-local-addr)
    endif()
    target_include_directories(test-launcherparser PRIVATE src)
    add_test(NAME launcherparser COMMAND test-launcherparser)

    add_executable(test-launchermodel
        tests/test_launchermodel.cpp
        src/launchermodel.cpp
        src/launchermodel.h
        src/launcherparser.cpp
        src/launcherparser.h
        src/iconloader.cpp
        src/iconloader.h
        src/iteminfo.h
        src/common.h
    )
    target_link_libraries(test-launchermodel PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::Test
    )
    target_compile_options(test-launchermodel PRIVATE
        -Wpedantic
        -pedantic
        -Werror=return-type
        -Werror=switch
        -Werror=uninitialized
        -Werror
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(test-launchermodel PRIVATE -Werror=return-stack-address)
    else()
        target_compile_options(test-launchermodel PRIVATE -Werror=return-local-addr)
    endif()
    target_compile_definitions(test-launchermodel PRIVATE
        CUSTOM_TOOLBOX_SYSCONFDIR="${CMAKE_CURRENT_BINARY_DIR}/test-config"
    )
    target_include_directories(test-launchermodel PRIVATE src)
    add_test(NAME launchermodel COMMAND test-launchermodel)
    set_tests_properties(launchermodel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_custom_target(check-install
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/install-smoke"
        COMMAND ${CMAKE_COMMAND} -E env "DESTDIR=${CMAKE_BINARY_DIR}/install-smoke"
                ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND}
                "-DINSTALL_ROOT=${CMAKE_BINARY_DIR}/install-smoke"
                "-DINSTALL_BINDIR=${CMAKE_INSTALL_FULL_BINDIR}"
                "-DINSTALL_SYSCONFDIR=${CMAKE_INSTALL_FULL_SYSCONFDIR}"
                "-DINSTALL_DATADIR=${CMAKE_INSTALL_FULL_DATADIR}"
                "-DINSTALL_MANDIR=${CMAKE_INSTALL_FULL_MANDIR}"
                -P "${CMAKE_SOURCE_DIR}/tests/verify-install.cmake"
        DEPENDS custom-toolbox
        USES_TERMINAL
    )
    add_test(NAME install-layout
        COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}" --target check-install
    )
endif()
