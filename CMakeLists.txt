cmake_minimum_required(VERSION 3.20)

project(manvil
    VERSION 0.1.0
    DESCRIPTION "Native Vulkan driver for Mali Valhall GPUs on Kbase"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(src)
