qt_add_executable(dokebrowser
  main.cpp
  core/AppController.cpp
  core/AppController.h
  core/IpcClient.cpp
  core/IpcClient.h
  core/LogListModel.cpp
  core/LogListModel.h
  core/ProfileFilterModel.cpp
  core/ProfileFilterModel.h
  core/ProfileListModel.cpp
  core/ProfileListModel.h
  core/ProxyPoolModel.cpp
  core/ProxyPoolModel.h
  core/ProfileRepository.cpp
  core/ProfileRepository.h
  qml/qml.qrc
  $<$<PLATFORM_ID:Windows>:win/app.rc>
)

target_link_libraries(dokebrowser PRIVATE
  dokebrowser_shared
  Qt6::Core
  Qt6::Gui
  Qt6::Network
  Qt6::Quick
  Qt6::Qml
  Qt6::QuickControls2
  Qt6::Sql
)

set_target_properties(dokebrowser PROPERTIES
  MACOSX_BUNDLE TRUE
  WIN32_EXECUTABLE TRUE
)

if(APPLE)
  set(APP_ICON "${CMAKE_SOURCE_DIR}/assets/DKIcon.icns")
  if(EXISTS "${APP_ICON}")
    set_source_files_properties("${APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(dokebrowser PRIVATE "${APP_ICON}")
    set_target_properties(dokebrowser PROPERTIES MACOSX_BUNDLE_ICON_FILE "DKIcon.icns")
  endif()
endif()

add_dependencies(dokebrowser dokebrowser_agent)

add_custom_command(TARGET dokebrowser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:dokebrowser_agent>
    $<TARGET_FILE_DIR:dokebrowser>/
)
