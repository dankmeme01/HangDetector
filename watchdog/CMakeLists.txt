cmake_minimum_required(VERSION 3.21)

project(watchdog)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)

add_executable(watchdog src/main.cpp)

set(ARC_FEATURE_NET ON CACHE BOOL "" FORCE)
CPMAddPackage("gh:dankmeme01/arc@1.5.5")
CPMAddPackage("gh:geode-sdk/json@3.3.0")

target_compile_definitions(${PROJECT_NAME} INTERFACE MAT_JSON_DYNAMIC=1)
target_link_libraries(watchdog PRIVATE arc mat-json dbghelp)
target_include_directories(watchdog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
