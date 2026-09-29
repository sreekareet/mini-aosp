# Interface + proxy: what clients link against.
add_library(vehicle_iface STATIC vehicle_iface.cpp)
target_link_libraries(vehicle_iface PUBLIC ipc)
mini_aosp_warnings(vehicle_iface)

# Implementation + stub: what only the service process links.
add_library(vehicle_service_lib STATIC vehicle_service.cpp)
target_link_libraries(vehicle_service_lib PUBLIC vehicle_iface)
mini_aosp_warnings(vehicle_service_lib)

add_executable(vehicle_service vehicle_service_main.cpp)
target_link_libraries(vehicle_service PRIVATE vehicle_service_lib hal_loader)
add_dependencies(vehicle_service fake_vehicle_hal)
mini_aosp_warnings(vehicle_service)
