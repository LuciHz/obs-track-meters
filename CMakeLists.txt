cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-audio-meters VERSION 1.0.0)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(obs-audio-meters MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

set(QT_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2024-09-05-x64")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets
  PATHS "${QT_DEPS_DIR}/lib/cmake/Qt6"
  NO_DEFAULT_PATH)

target_sources(
  obs-audio-meters
  PRIVATE
    src/plugin-main.cpp
    src/audio-meter.cpp
    src/audio-meter.hpp)

target_link_libraries(obs-audio-meters
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets)

target_include_directories(obs-audio-meters SYSTEM PRIVATE
  "${QT_DEPS_DIR}/include"
  "${QT_DEPS_DIR}/include/QtCore"
  "${QT_DEPS_DIR}/include/QtWidgets"
  "${QT_DEPS_DIR}/include/QtGui")

target_include_directories(obs-audio-meters PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties(obs-audio-meters PROPERTIES AUTOMOC ON AUTOUIC ON)

set_target_properties_plugin(obs-audio-meters PROPERTIES BUILD_NUMBER 1)