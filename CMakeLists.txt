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
    src/mailview.cpp
    src/sidebar.cpp
    src/mainwindow.cpp
    data/patchwerk.qrc
)
target_link_libraries(patchwerk PRIVATE Qt6::Widgets Qt6::Network)
include(GNUInstallDirs)
install(TARGETS patchwerk)
install(FILES data/patchwerk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
