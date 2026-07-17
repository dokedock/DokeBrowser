add_executable(dokebrowser_smoke
  smoke_main.cpp
)

target_link_libraries(dokebrowser_smoke PRIVATE
  dokebrowser_shared
  Qt6::Core
  Qt6::Network
)

add_executable(dokebrowser_engine_config
  engine_config_main.cpp
)

target_link_libraries(dokebrowser_engine_config PRIVATE
  dokebrowser_agent_core
)

add_executable(dokebrowser_profile_launch_config
  profile_launch_config_main.cpp
)

target_link_libraries(dokebrowser_profile_launch_config PRIVATE
  dokebrowser_agent_core
)

add_executable(dokebrowser_proxy_test_runner
  proxy_test_runner_main.cpp
)

target_link_libraries(dokebrowser_proxy_test_runner PRIVATE
  dokebrowser_agent_core
)
