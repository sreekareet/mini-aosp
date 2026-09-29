include(FetchContent)

if(MINI_AOSP_SYSTEM_GTEST)
  find_package(GTest REQUIRED)  # from libgtest-dev
else()
  # Pinned release tag. If the download fails, re-run cmake with
  # -DMINI_AOSP_SYSTEM_GTEST=ON (after: sudo apt install libgtest-dev).
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# mini_aosp_test(<name> SOURCES a.cpp ... LIBS lib ...) -> one test executable per layer.
function(mini_aosp_test name)
  cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${T_SOURCES})
  target_link_libraries(${name} PRIVATE ${T_LIBS} GTest::gtest_main)
  target_compile_definitions(${name} PRIVATE
    BIN_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" LIB_DIR="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
  mini_aosp_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

mini_aosp_test(hal_tests SOURCES hal_test.cpp LIBS hal_loader)
add_dependencies(hal_tests fake_vehicle_hal)

mini_aosp_test(common_tests SOURCES common_test.cpp LIBS ipc)

mini_aosp_test(ipc_tests SOURCES ipc_test.cpp LIBS ipc)

mini_aosp_test(service_tests SOURCES service_test.cpp LIBS vehicle_service_lib hal_loader)
add_dependencies(service_tests servicemanager vehicle_service fake_vehicle_hal)

mini_aosp_test(framework_tests SOURCES framework_test.cpp LIBS framework vehicle_service_lib hal_loader)
add_dependencies(framework_tests servicemanager vehicle_service fake_vehicle_hal)
