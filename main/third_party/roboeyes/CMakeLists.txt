cmake_minimum_required(VERSION 3.16)

# This CMakeLists is a helper to optionally include vendored RoboEyes sources.
file(GLOB ROBOEYES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
if(ROBOEYES_SRC)
    message(STATUS "Found RoboEyes sources: ${ROBOEYES_SRC}")
    # Provide variable for top-level CMake to consume if needed.
    set(ROBOEYES_SOURCES ${ROBOEYES_SRC} PARENT_SCOPE)
    set(ROBOEYES_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src PARENT_SCOPE)
else()
    message(STATUS "No RoboEyes sources found in ${CMAKE_CURRENT_SOURCE_DIR} (expected under src/).")
endif()

