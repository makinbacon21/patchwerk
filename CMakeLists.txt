# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Thomas Makin

cmake_minimum_required(VERSION 3.21)
project(patchwerk VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network)
find_package(KF6SyntaxHighlighting REQUIRED)
find_path(TREE_SITTER_INCLUDE_DIR tree_sitter/api.h REQUIRED)
find_library(TREE_SITTER_LIB tree-sitter REQUIRED)
set(GRAMMARS c rust python bash yaml json)
foreach(lang IN LISTS GRAMMARS)
    find_library(TREE_SITTER_${lang}_LIB tree-sitter-${lang} REQUIRED)
    list(APPEND GRAMMAR_LIBS ${TREE_SITTER_${lang}_LIB})
endforeach()

add_executable(patchwerk
    src/main.cpp
    src/mail.cpp
    src/series.cpp
    src/project.cpp
    src/tools.cpp
    src/llm.cpp
    src/settings.cpp
    src/snackbar.cpp
    src/diffview.cpp
    src/highlight.cpp
    src/mailview.cpp
    src/sidebar.cpp
    src/mainwindow.cpp
    data/patchwerk.qrc
)
target_include_directories(patchwerk PRIVATE ${TREE_SITTER_INCLUDE_DIR})
target_link_libraries(patchwerk PRIVATE Qt6::Widgets Qt6::Network KF6::SyntaxHighlighting
                      ${TREE_SITTER_LIB} ${GRAMMAR_LIBS})
include(GNUInstallDirs)
install(TARGETS patchwerk)
install(FILES data/patchwerk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
