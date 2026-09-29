cmake_minimum_required(VERSION 3.16)
project(mini_aosp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Everything lands in build/bin (executables) and build/lib (shared libraries), so
# binaries can find each other and the HAL plugin relative to their own location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(MINI_AOSP_WERROR "Treat warnings as errors" ON)
option(MINI_AOSP_SYSTEM_GTEST "Use the system GoogleTest (libgtest-dev) instead of downloading it" OFF)
set(MINI_AOSP_SANITIZER "" CACHE STRING "Sanitizer build: address (ASan+UBSan), thread (TSan), or empty")
set_property(CACHE MINI_AOSP_SANITIZER PROPERTY STRINGS "" address thread)

if(MINI_AOSP_SANITIZER STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif(MINI_AOSP_SANITIZER STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(NOT MINI_AOSP_SANITIZER STREQUAL "")
  message(FATAL_ERROR "Unknown MINI_AOSP_SANITIZER '${MINI_AOSP_SANITIZER}'")
endif()

# Warnings are applied per target (not globally) so third-party code (GoogleTest)
# is not subject to our -Werror.
function(mini_aosp_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(MINI_AOSP_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

# Header-only "libutils": include root (so code says #include "hal/vehicle_hal.h"),
# threads and dl.
find_package(Threads REQUIRED)
add_library(mini_common INTERFACE)
target_include_directories(mini_common INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(mini_common INTERFACE Threads::Threads ${CMAKE_DL_LIBS})

add_subdirectory(common)
add_subdirectory(hal)
add_subdirectory(ipc)
add_subdirectory(services/vehicle)
add_subdirectory(init)
add_subdirectory(framework)
add_subdirectory(apps)

enable_testing()
add_subdirectory(tests)
