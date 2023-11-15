if(POLICY CMP0087)
  cmake_policy(SET CMP0087 NEW)
endif()

set(OBS_STANDALONE_PLUGIN_DIR ${CMAKE_SOURCE_DIR}/release)

include(GNUInstallDirs)
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  set(OS_MACOS ON)
  set(OS_POSIX ON)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux|FreeBSD|OpenBSD")
  set(OS_POSIX ON)
  string(TOUPPER "${CMAKE_SYSTEM_NAME}" _SYSTEM_NAME_U)
  set(OS_${_SYSTEM_NAME_U} ON)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  set(OS_WINDOWS ON)
  set(OS_POSIX OFF)
endif()

# Set default Qt version to AUTO, preferring an available Qt6 with a fallback to Qt5
if(NOT QT_VERSION)
  set(QT_VERSION
      AUTO
      CACHE STRING "OBS Qt version [AUTO, 6, 5]" FORCE)
  set_property(CACHE QT_VERSION PROPERTY STRINGS AUTO 6 5)
endif()

# Macro to find best possible Qt version for use with the project:
#
# * Use QT_VERSION value as a hint for desired Qt version
# * If "AUTO" was specified, prefer Qt6 over Qt5
# * Creates versionless targets of desired component if none had been created by Qt itself (Qt
#   versions < 5.15)
#
macro(find_qt)
  set(multiValueArgs COMPONENTS COMPONENTS_WIN COMPONENTS_MAC COMPONENTS_LINUX)
  cmake_parse_arguments(FIND_QT "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Do not use versionless targets in the first step to avoid Qt::Core being clobbered by later
  # opportunistic find_package runs
  set(QT_NO_CREATE_VERSIONLESS_TARGETS ON)

  # Loop until _QT_VERSION is set or FATAL_ERROR aborts script execution early
  while(NOT _QT_VERSION)
    if(QT_VERSION STREQUAL AUTO AND NOT _QT_TEST_VERSION)
      set(_QT_TEST_VERSION 6)
    elseif(NOT QT_VERSION STREQUAL AUTO)
      set(_QT_TEST_VERSION ${QT_VERSION})
    endif()

    find_package(
      Qt${_QT_TEST_VERSION}
      COMPONENTS Core
      QUIET)

    if(TARGET Qt${_QT_TEST_VERSION}::Core)
      set(_QT_VERSION
          ${_QT_TEST_VERSION}
          CACHE INTERNAL "")
      message(STATUS "Qt version found: ${_QT_VERSION}")
      unset(_QT_TEST_VERSION)
      break()
    elseif(QT_VERSION STREQUAL AUTO)
      if(_QT_TEST_VERSION EQUAL 6)
        message(WARNING "Qt6 was not found, falling back to Qt5")
        set(_QT_TEST_VERSION 5)
        continue()
      endif()
    endif()
    message(FATAL_ERROR "Neither Qt6 nor Qt5 found.")
  endwhile()

  # Enable versionless targets for the remaining Qt components
  set(QT_NO_CREATE_VERSIONLESS_TARGETS OFF)

  set(_QT_COMPONENTS ${FIND_QT_COMPONENTS})
  if(OS_WINDOWS)
    list(APPEND _QT_COMPONENTS ${FIND_QT_COMPONENTS_WIN})
  elseif(OS_MACOS)
    list(APPEND _QT_COMPONENTS ${FIND_QT_COMPONENTS_MAC})
  else()
    list(APPEND _QT_COMPONENTS ${FIND_QT_COMPONENTS_LINUX})
  endif()

  find_package(
    Qt${_QT_VERSION}
    COMPONENTS ${_QT_COMPONENTS}
    REQUIRED)

  list(APPEND _QT_COMPONENTS Core)

  if("Gui" IN_LIST FIND_QT_COMPONENTS_LINUX)
    list(APPEND _QT_COMPONENTS "GuiPrivate")
  endif()

  # Check for versionless targets of each requested component and create if necessary
  foreach(_COMPONENT IN LISTS _QT_COMPONENTS)
    if(NOT TARGET Qt::${_COMPONENT} AND TARGET Qt${_QT_VERSION}::${_COMPONENT})
      add_library(Qt::${_COMPONENT} INTERFACE IMPORTED)
      set_target_properties(Qt::${_COMPONENT} PROPERTIES INTERFACE_LINK_LIBRARIES
                                                         Qt${_QT_VERSION}::${_COMPONENT})
    endif()
  endforeach()
endmacro()
