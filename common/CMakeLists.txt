add_library(common STATIC event_loop.cpp)
target_link_libraries(common PUBLIC mini_common)
mini_aosp_warnings(common)
