add_executable(dokebrowser_agent
  main.cpp
)

target_link_libraries(dokebrowser_agent PRIVATE
  Qt6::Core
)

