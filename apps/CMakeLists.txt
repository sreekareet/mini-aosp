add_executable(hal_demo hal_demo.cpp)
target_link_libraries(hal_demo PRIVATE hal_loader)
mini_aosp_warnings(hal_demo)

add_executable(vehicle_cli vehicle_cli.cpp)
target_link_libraries(vehicle_cli PRIVATE framework)
mini_aosp_warnings(vehicle_cli)
