# Specify the minimum version of Cmake
cmake_minimum_required (VERSION 2.6)

# Project's name
project (smartqq)

# add the executable
add_executable (smartqq main.cpp client.cpp api.cpp model.cpp robot.cpp utils.cpp)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -std=c++11 -stdlib=libc++")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -DSMARTQQ_DEBUG")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS}")

find_package(CURL)
if(CURL_FOUND)
    set(USE_SYSTEM_CURL ON CACHE BOOL "Use the system curl for faster builds")
endif()

set(BUILD_CPR_TESTS OFF)

set (SMARTQQ_INCLUDE_DIRS "include")
set (JSON_INCLUDE_DIR "json")
include_directories(${SMARTQQ_INCLUDE_DIRS} ${JSON_INCLUDE_DIR})

add_subdirectory(cpr)
include_directories(${CPR_INCLUDE_DIRS})
target_link_libraries(smartqq ${CPR_LIBRARIES} pthread)

