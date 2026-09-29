# The vendor plugin: a shared library loaded at runtime, NOT linked into anything.
add_library(fake_vehicle_hal SHARED fake_vehicle_hal.cpp)
target_link_libraries(fake_vehicle_hal PRIVATE mini_common)
mini_aosp_warnings(fake_vehicle_hal)

# The platform side: the loader.
add_library(hal_loader STATIC hal_loader.cpp)
target_link_libraries(hal_loader PUBLIC mini_common)
mini_aosp_warnings(hal_loader)
