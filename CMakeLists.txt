cmake_minimum_required(VERSION 3.10)

project(vesuvius C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_executable(vesuvius_example example.c)
add_executable(vesuvius_example2 example2.c)
add_executable(vesuvius_tests runtests.c)

include_directories(third-party/json.h)

find_package(Blosc2 REQUIRED)
find_package(CURL REQUIRED)

add_compile_options(-g3 -Wall -Wextra)
if(UNIX)
    add_link_options(-rdynamic)
endif()

# For some reason, when using _just_ link_libraries, no libraries are actually linked because ???
# so just target_link_libraries for all executables
target_link_libraries(vesuvius_example PUBLIC -lm)
target_link_libraries(vesuvius_example2 PUBLIC -lm)
target_link_libraries(vesuvius_tests PUBLIC -lm)

if(Blosc2_FOUND)
    message(STATUS "Found blosc2. Building with Zarr support")

    target_link_libraries(vesuvius_example PUBLIC Blosc2::Blosc2)
    target_link_libraries(vesuvius_example2 PUBLIC Blosc2::Blosc2)
    target_link_libraries(vesuvius_tests PUBLIC Blosc2::Blosc2)
    add_compile_definitions(VESUVIUS_ZARR_IMPL)
else()
    message(STATUS "Blosc2 not found - building without Zarr support")
endif()

if(CURL_FOUND)
    message(STATUS "Found curl. Building with Curl support")
    target_link_libraries(vesuvius_example PUBLIC CURL::libcurl)
    target_link_libraries(vesuvius_example2 PUBLIC CURL::libcurl)
    target_link_libraries(vesuvius_tests PUBLIC CURL::libcurl)
    add_compile_definitions(VESUVIUS_CURL_IMPL)
else()
    message(STATUS "CURL not found - building without CURL support")
endif()


