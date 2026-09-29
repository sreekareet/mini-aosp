add_library(framework STATIC property_manager.cpp)
target_link_libraries(framework PUBLIC vehicle_iface)
mini_aosp_warnings(framework)
