qt_add_executable(dokebrowser
  main.cpp
  qml/qml.qrc
  $<$<PLATFORM_ID:Windows>:win/app.rc>
)

target_link_libraries(dokebrowser PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::Qml
  Qt6::QuickControls2
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

