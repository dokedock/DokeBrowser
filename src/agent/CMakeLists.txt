add_library(dokebrowser_agent_core STATIC
  core/IpcServer.cpp
  core/IpcServer.h
  core/BrowserEngineFactory.cpp
  core/BrowserEngineFactory.h
  core/BrowserEngine.h
  core/CdpClient.cpp
  core/CdpClient.h
  core/DokeChromiumEngine.cpp
  core/DokeChromiumEngine.h
  core/FingerprintMetadata.cpp
  core/FingerprintMetadata.h
  core/HttpProxyMapper.cpp
  core/HttpProxyMapper.h
  core/ProfileLaunchConfig.cpp
  core/ProfileLaunchConfig.h
  core/ProxyTestRunner.cpp
  core/ProxyTestRunner.h
  core/SystemChromeEngine.cpp
  core/SystemChromeEngine.h
)

target_link_libraries(dokebrowser_agent_core PUBLIC
  dokebrowser_shared
  Qt6::Core
  Qt6::Network
  Qt6::WebSockets
)

target_include_directories(dokebrowser_agent_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(dokebrowser_agent
  main.cpp
)

target_link_libraries(dokebrowser_agent PRIVATE
  dokebrowser_agent_core
)
