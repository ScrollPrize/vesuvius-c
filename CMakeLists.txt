cmake_minimum_required(VERSION 3.10)

project(vesuvius C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_executable(vesuvius_example example.c)
add_executable(vesuvius_example2 example2.c)
add_executable(vesuvius_tests runtests.c)
add_executable(vesuvius_tests_sanitizer runtests.c)

include_directories(third-party/json.h)

add_compile_options(-g3 -Wall -Wextra)

# For some reason, when using _just_ link_libraries, no libraries are actually linked because ???
# so just target_link_libraries for all executables
if(UNIX)
    # -rdynamic allows for the ASSERT backtrace to include symbols on UNIX
    target_link_options(vesuvius_example PUBLIC -rdynamic)
    target_link_options(vesuvius_example2 PUBLIC -rdynamic)
    target_link_options(vesuvius_tests PUBLIC -rdynamic)
    target_link_options(vesuvius_tests_sanitizer PUBLIC -rdynamic)
endif()

target_link_libraries(vesuvius_example PUBLIC -lm)
target_link_libraries(vesuvius_example2 PUBLIC -lm)
target_link_libraries(vesuvius_tests PUBLIC -lm)
target_link_libraries(vesuvius_tests_sanitizer PUBLIC -lm)

find_package(Blosc2 REQUIRED)
find_package(CURL REQUIRED)

if(Blosc2_FOUND)
    message(STATUS "Found blosc2. Building with Zarr support")

    target_link_libraries(vesuvius_example PUBLIC Blosc2::Blosc2)
    target_link_libraries(vesuvius_example2 PUBLIC Blosc2::Blosc2)
    target_link_libraries(vesuvius_tests PUBLIC Blosc2::Blosc2)
    target_link_libraries(vesuvius_tests_sanitizer PUBLIC Blosc2::Blosc2)
else()
    message(FATAL_ERROR "Blosc2 not found, please install blosc2")
endif()

if(CURL_FOUND)
    message(STATUS "Found curl. Building with Curl support")
    target_link_libraries(vesuvius_example PUBLIC CURL::libcurl)
    target_link_libraries(vesuvius_example2 PUBLIC CURL::libcurl)
    target_link_libraries(vesuvius_tests PUBLIC CURL::libcurl)
    target_link_libraries(vesuvius_tests_sanitizer PUBLIC CURL::libcurl)
else()
    message(FATAL_ERROR "CURL not found, please install curl")
endif()

target_compile_options(vesuvius_tests_sanitizer PUBLIC -fsanitize=address -fno-omit-frame-pointer)
target_link_options(vesuvius_tests_sanitizer PUBLIC -fsanitize=address)
