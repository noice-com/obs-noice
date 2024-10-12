if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(STANDALONE OFF)
  if(DISABLE_UI OR NOT ENABLE_UI)
    set(OBS_FRONTEND_AVAILABLE OFF)
  else()
    set(OBS_FRONTEND_AVAILABLE ON)
  endif()
else()
  set(STANDALONE ON)
endif()

cmake_minimum_required(VERSION 3.16...3.21)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(noice VERSION 1.4.0)
set(NOICE_ROOT_VERSION ${PROJECT_VERSION})

add_library(${PROJECT_NAME} MODULE)

if(CMAKE_PREFIX_PATH)
  list(APPEND _OBS_CMAKE_TARGETS "libobs" "LibObs" "w32-pthreads" "obs-frontend-api")
  foreach(T ${_OBS_CMAKE_TARGETS})
    set(${T}_ROOT "${CMAKE_PREFIX_PATH}/lib/cmake;${CMAKE_PREFIX_PATH}/cmake")
  endforeach()
endif()

set(PLUGIN_AUTHOR "Noice Inc.")
set(PROJECT_TITLE "Noice (for OBS Studio)")
set(PROJECT_COPYRIGHT "2023, Noice Inc. All Rights Reserved")
set(MACOS_BUNDLEID "com.noice.obs-plugin")
set(MACOS_BUNDLEID_UI "com.noice.obs-ui-plugin")
set(LINUX_MAINTAINER_EMAIL "aet@noice.com")

# Import libobs as main plugin dependency
if(STANDALONE)
  set(OBS_AVAILABLE ON)
  find_package("libobs" CONFIG)
  if(NOT libobs_FOUND)
    find_package("LibObs" CONFIG)
    set(OBS_AVAILABLE LibObs_FOUND)
  endif()

  if(NOT OBS_AVAILABLE)
    message(FATAL_ERROR "Can't find libobs or LibObs package")
  endif()
  include(cmake/ObsPluginHelpers.cmake)
  if(OS_WINDOWS)
    find_package("w32-pthreads" REQUIRED)
  endif()
endif()

if(NOT TARGET OBS::libobs)
  set(MODERN_OBS OFF)
  add_library(OBS::libobs ALIAS libobs)
  if(OS_WINDOWS)
    add_library(OBS::w32-pthreads ALIAS w32-pthreads)
  endif()
else()
  set(MODERN_OBS ON)
endif()

if(STANDALONE)
  set(OBS_FRONTEND_AVAILABLE ON)
  if(NOT TARGET obs-frontend-api)
    find_package("obs-frontend-api" CONFIG)
    if(NOT obs-frontend-api_FOUND)
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_lib_suffix 64)
      else()
        set(_lib_suffix 32)
      endif()
      find_library(
        OBS_FRONTEND_LIB
        NAMES obs-frontend-api libobs-frontend-api
        PATH_SUFFIXES lib${_lib_suffix} lib bin${_lib_suffix} bin/${_lib_suffix}bit bin)
      if(NOT OBS_FRONTEND_LIB)
        set(OBS_FRONTEND_AVAILABLE OFF)
      endif()
    endif()
  endif()
endif()

if(OBS_FRONTEND_AVAILABLE)
  if(TARGET OBS::obs-frontend-api)
    set(OBS_FRONTEND_LIB OBS::obs-frontend-api)
  elseif(TARGET obs-frontend-api)
    set(OBS_FRONTEND_LIB obs-frontend-api)
  endif()
endif()

option(ENABLE_OBS_UI "Enables OBS UI integration" ON)
if(DISABLE_OBS_UI OR NOT ENABLE_OBS_UI)
  set(OBS_UI_ENABLED OFF)
else()
  set(OBS_UI_ENABLED ON)
endif()

message(
  STATUS
    "Standalone: ${STANDALONE} Modern OBS: ${MODERN_OBS}: Frontend: ${OBS_FRONTEND_AVAILABLE} UI: ${OBS_UI_ENABLED} CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}"
)

target_sources(
  ${PROJECT_NAME}
  PRIVATE "source/plugin.cpp"
          "source/noice-bridge.hpp"
          "source/noice-bridge.cpp"
          "source/obs-bridge.hpp"
          "source/obs-bridge.cpp"
          "source/common.hpp"
          "source/common.cpp"
          "source/game.hpp"
          "source/game.cpp"
          "source/noice-validator.hpp"
          "source/noice-validator.cpp"
          "source/scene-tracker.hpp"
          "source/scene-tracker.cpp"
          "source/auth.hpp"
          "source/auth.cpp"
          "source/obs/obs-source-factory.hpp"
          "source/obs/obs-source.hpp"
          "source/util/util.hpp"
          "source/util/util-curl.hpp"
          "source/util/util-curl.cpp"
          "deps/file-updater/file-updater.hpp"
          "deps/file-updater/file-updater.cpp")
target_compile_definitions(${PROJECT_NAME} PRIVATE NOICE_CORE)

# Prefer system provided libcurl over others as that's what OBS uses
if(OS_MACOS)
  set(CURL_INCLUDE_DIR "${CMAKE_OSX_SYSROOT}/usr/include")
  set(CURL_LIBRARY "${CMAKE_OSX_SYSROOT}/usr/lib/libcurl.tbd")
endif()

find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)

configure_file(source/version.h.in ${PROJECT_SOURCE_DIR}/source/version.h)
target_sources(${PROJECT_NAME} PRIVATE source/version.h)

# --- Platform-independent build settings ---

target_include_directories(
  ${PROJECT_NAME} PRIVATE "${PROJECT_BINARY_DIR}/source" "${PROJECT_SOURCE_DIR}/source"
                          "${PROJECT_SOURCE_DIR}/deps")

target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)
if(OS_WINDOWS)
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::w32-pthreads)
endif()

if(OS_MACOS)
  target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreServices"
                                                "-framework CoreFoundation")
endif()

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  # Visual Studio Generator has not been supported supression warnings of external headers yet.
  # https://gitlab.kitware.com/cmake/cmake/-/issues/17904
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(VISUALSTUDIO_GENERATOR 1)
  else()
    set(VISUALSTUDIO_GENERATOR 0)
  endif()

  target_compile_options(
    ${PROJECT_NAME}
    PUBLIC $<$<NOT:${VISUALSTUDIO_GENERATOR}>:-Werror>
           -Wall
           -Wextra
           -Wformat=2
           -Wno-attributes
           -Wno-unknown-pragmas
           -Wno-unused-parameter
           -Wno-unused-command-line-argument)
endif()

# --- End of section ---

# --- Windows-specific build settings and tasks ---
if(OS_WINDOWS)
  configure_file(cmake/bundle/windows/installer-Windows.iss.in
                 ${CMAKE_BINARY_DIR}/installer-Windows.generated.iss)

  configure_file(cmake/bundle/windows/resource.rc.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.rc)
  target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.rc)

  if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    # * Disable useless warnings
    set(DISABLED_WARNINGS
        # Don't warn about unused variables, parameters, labels, functions, or typedefs.
        "4100"
        "4101"
        "4102"
        "4505"
        "4514"
        "5245"
        # Don't warn about unreferenced variables or parameters which are assigned/initialized.
        "4189"
        # Don't warn about not-explicitly-handled enumeration identifiers
        "4061"
        # Unknown pragmas
        "4068"
        # Declaration hides class member
        "4458"
        # Relative include paths are fine.
        "4464"
        # Unreachable code
        "4702"
        # Non-portable use of class
        "4840"
        # Buggy warning: subobject initialization should be wrapped in braces
        "5246")
    foreach(WARN ${DISABLED_WARNINGS})
      target_compile_options(${PROJECT_NAME} PRIVATE "/wd${WARN}")
    endforeach()
  endif()
  # --- End of section ---

  # -- macOS specific build settings and tasks --
elseif(OS_MACOS)
  function(SET_PACKAGES_NAME VAR _PLUGIN_NAME)
    if(MODERN_OBS)
      set(${VAR}
          "${_PLUGIN_NAME}.plugin"
          PARENT_SCOPE)
    else()
      set(${VAR}
          "${_PLUGIN_NAME}"
          PARENT_SCOPE)
    endif()
  endfunction()

  set_packages_name(PACKAGES_PATH_NAME "${PROJECT_NAME}")
  if(OBS_FRONTEND_AVAILABLE AND OBS_UI_ENABLED)
    set_packages_name(PACKAGES_PATH_NAME_UI "noice_ui")
  else()
    set_packages_name(PACKAGES_PATH_NAME_UI "${PROJECT_NAME}")
  endif()

  configure_file(cmake/bundle/macos/installer-macos.pkgproj.in
                 ${CMAKE_BINARY_DIR}/installer-macos.generated.pkgproj)

  set(MACOSX_PLUGIN_BUNDLE_NAME "${MACOS_BUNDLEID}")
  set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
  set(MACOSX_BUNDLE_COPYRIGHT "${PLUGIN_AUTHOR}")
  set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "${NOICE_ROOT_VERSION}")
  set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")

  target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
  # --- End of section ---

  # --- Linux-specific build settings and tasks ---
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
endif()
# --- End of section ---

function(SET_OBS_PLUGIN _PLUGIN_NAME)
  if(MODERN_OBS OR STANDALONE)
    if(NOT MODERN_OBS AND OS_MACOS)
      set_target_properties(${_PLUGIN_NAME} PROPERTIES PREFIX "")
      install(TARGETS ${_PLUGIN_NAME}
              LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${_PLUGIN_NAME}/bin)
      install(DIRECTORY data DESTINATION ${CMAKE_INSTALL_PREFIX}/${_PLUGIN_NAME})
    else()
      if(COMMAND setup_plugin_target)
        setup_plugin_target(${_PLUGIN_NAME})
      elseif(COMMAND set_target_properties_obs)
        set_target_properties_obs(${_PLUGIN_NAME} PROPERTIES FOLDER plugins PREFIX "")
      endif()
    endif()
  else()
    set_target_properties(${_PLUGIN_NAME} PROPERTIES FOLDER "plugins/${_PLUGIN_NAME}")
    install_obs_plugin_with_data(${_PLUGIN_NAME} data)
  endif()
endfunction()

set_obs_plugin(${PROJECT_NAME})

if(OBS_FRONTEND_AVAILABLE AND OBS_UI_ENABLED)
  add_subdirectory(ui)
  add_dependencies(noice noice_ui)
endif()
