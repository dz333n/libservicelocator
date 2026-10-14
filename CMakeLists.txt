cmake_minimum_required(VERSION 3.16)
project(servicelocator VERSION 1.0.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(servicelocator INTERFACE)
add_library(servicelocator::servicelocator ALIAS servicelocator)
target_include_directories(servicelocator INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(servicelocator INTERFACE cxx_std_17)
target_link_libraries(servicelocator INTERFACE Threads::Threads)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(servicelocator_is_top_level ON)
else()
    set(servicelocator_is_top_level OFF)
endif()
option(SERVICELOCATOR_BUILD_TESTS "Build the libservicelocator tests" ${servicelocator_is_top_level})

if(SERVICELOCATOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
