add_executable(hal_demo hal_demo.cpp)
target_link_libraries(hal_demo PRIVATE hal_loader)
mini_aosp_warnings(hal_demo)
