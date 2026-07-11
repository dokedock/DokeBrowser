add_executable(dokebrowser_agent
  main.cpp
  core/IpcServer.cpp
  core/IpcServer.h
)

target_link_libraries(dokebrowser_agent PRIVATE
  dokebrowser_shared
  Qt6::Core
  Qt6::Network
)
