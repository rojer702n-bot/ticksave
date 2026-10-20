cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Is GEODE_SDK defined?")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

project(ticksave VERSION 1.0.0)

target_include_directories(ticksave PRIVATE src)
setup_geode_mod(ticksave)
