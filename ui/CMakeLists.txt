project(noice_ui VERSION ${NOICE_ROOT_VERSION})

set(MACOS_BUNDLEID "${MACOS_BUNDLEID_UI}")
set(MACOSX_PLUGIN_BUNDLE_NAME "${MACOS_BUNDLEID_UI}")
set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID_UI}")

add_library(${PROJECT_NAME} MODULE)

target_link_libraries(${PROJECT_NAME} PRIVATE ${OBS_FRONTEND_LIB})

if(NOT STANDALONE AND NOT MODERN_OBS)
  include(../cmake/QtHelper.cmake)
endif()

find_qt(COMPONENTS Widgets Core)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)

target_sources(
  ${PROJECT_NAME}
  PRIVATE "ui-plugin.cpp"
          "../source/noice-bridge.hpp"
          "../source/noice-bridge.cpp"
          "obs/obs-browser.hpp"
          "obs/obs-browser.cpp"
          "ui.hpp"
          "ui.cpp"
          "ui-dock-chat.hpp"
          "ui-dock-chat.cpp"
          "ui-dock-eventlist.hpp"
          "ui-dock-eventlist.cpp"
          "ui-dock-stats.hpp"
          "ui-dock-stats.cpp"
          "ui-frame-stats.hpp"
          "ui-frame-stats.cpp")
target_compile_definitions(${PROJECT_NAME} PRIVATE UI_ENABLED)

target_link_libraries(${PROJECT_NAME} PRIVATE "$<TARGET_PROPERTY:noice,LINK_LIBRARIES>")
target_include_directories(${PROJECT_NAME} PRIVATE "$<TARGET_PROPERTY:noice,INCLUDE_DIRECTORIES>")
target_compile_options(${PROJECT_NAME} PRIVATE "$<TARGET_PROPERTY:noice,COMPILE_OPTIONS>")
target_link_options(${PROJECT_NAME} PRIVATE "$<TARGET_PROPERTY:noice,LINK_OPTIONS>")

set_obs_plugin(${PROJECT_NAME})
