add_library(ipc STATIC
  parcel.cpp wire.cpp transport.cpp remote.cpp rpc_server.cpp rpc_client.cpp service_manager.cpp)
target_link_libraries(ipc PUBLIC common)
mini_aosp_warnings(ipc)

add_executable(servicemanager servicemanager_main.cpp)
target_link_libraries(servicemanager PRIVATE ipc)
mini_aosp_warnings(servicemanager)
