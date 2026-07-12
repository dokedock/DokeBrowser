set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network WebSockets Quick Qml QuickControls2 Sql)

add_library(dokebrowser_shared STATIC
  shared/ipc/FramedJsonSocket.cpp
  shared/ipc/FramedJsonSocket.h
)

target_link_libraries(dokebrowser_shared PUBLIC
  Qt6::Core
  Qt6::Network
)

target_include_directories(dokebrowser_shared PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(app)
add_subdirectory(agent)
add_subdirectory(tests)
