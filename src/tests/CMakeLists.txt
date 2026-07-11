add_executable(dokebrowser_smoke
  smoke_main.cpp
)

target_link_libraries(dokebrowser_smoke PRIVATE
  dokebrowser_shared
  Qt6::Core
  Qt6::Network
)

