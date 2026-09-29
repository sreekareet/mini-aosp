# The property store and its IPC service (also used by clients through the framework).
add_library(property_lib STATIC property_store.cpp property_service.cpp)
target_link_libraries(property_lib PUBLIC ipc)
mini_aosp_warnings(property_lib)

add_library(init_lib STATIC config.cpp supervisor.cpp)
target_link_libraries(init_lib PUBLIC property_lib)
mini_aosp_warnings(init_lib)

add_executable(init init_main.cpp)
target_link_libraries(init PRIVATE init_lib)
add_dependencies(init servicemanager vehicle_service)
mini_aosp_warnings(init)

# Ship the config next to the binaries: build/etc/init.conf
configure_file(init.conf ${CMAKE_BINARY_DIR}/etc/init.conf COPYONLY)
