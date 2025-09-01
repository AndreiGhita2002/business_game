cmake_minimum_required(VERSION 4.0)
project(business_game)

# raylib
find_package(raylib QUIET)
if(NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5e6cdf3
            GIT_SHALLOW 1
    )
    FetchContent_MakeAvailable(raylib)
endif()

# raylib-cpp
find_package(raylib_cpp QUIET)
if(NOT raylib_cpp_FOUND)
    # Use a real tag/branch/sha instead of "next" (which is not valid here).
    # Users can -D RAYLIB_CPP_VERSION=<tag/sha> to override.
    set(RAYLIB_CPP_VERSION "master" CACHE STRING "raylib-cpp git tag/branch/sha")
    include(FetchContent)
    FetchContent_Declare(
            raylib_cpp
            GIT_REPOSITORY https://github.com/RobLoach/raylib-cpp.git
            GIT_TAG ${RAYLIB_CPP_VERSION}
    )
    FetchContent_MakeAvailable(raylib_cpp)
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Main executable ===
add_executable(${PROJECT_NAME}
        ${CMAKE_SOURCE_DIR}/src/game/main.cpp
        ${CMAKE_SOURCE_DIR}/src/game/main.hpp
        src/game/gameMap.cpp
        src/game/gameMap.hpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib raylib_cpp)

# macOS frameworks
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit")
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa")
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework OpenGL")
endif()

# Web (Emscripten) configuration
if(PLATFORM STREQUAL "Web")
    # Produce an HTML output for Emscripten builds
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

    # Required linker flags for using raylib with Emscripten
    target_link_options(${PROJECT_NAME} PRIVATE
            -sEXPORTED_FUNCTIONS=['_main','_malloc']
            -sEXPORTED_RUNTIME_METHODS=ccall
            -sUSE_GLFW=3
    )
endif()

# === Output dirs ===
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
