add_executable(dokebrowser_agent
  main.cpp
  core/IpcServer.cpp
  core/IpcServer.h
  core/CdpClient.cpp
  core/CdpClient.h
  core/HttpProxyMapper.cpp
  core/HttpProxyMapper.h
)

target_link_libraries(dokebrowser_agent PRIVATE
  dokebrowser_shared
  Qt6::Core
  Qt6::Network
  Qt6::WebSockets
)
