cmake_minimum_required(VERSION 4.0)
project(business_game)
include(FetchContent)

# raylib
find_package(raylib QUIET)
if(NOT raylib_FOUND)
    include(FetchContent)
    # Pinned on purpose. Without a tag this tracks raylib's default branch,
    # which has moved on to 6.x, and raylib-cpp refuses to build against
    # anything past 5.x. 5.5 is the newest release raylib-cpp accepts.
    FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
            GIT_SHALLOW 1
    )
    FetchContent_MakeAvailable(raylib)
endif()

# raylib-cpp
find_package(raylib_cpp QUIET)
if(NOT raylib_cpp_FOUND)
    # Pinned to a commit rather than a branch, so a rebuild cannot pick up an
    # incompatible raylib-cpp on its own. This one targets raylib 5.x.
    # Users can -D RAYLIB_CPP_VERSION=<tag/sha> to override.
    set(RAYLIB_CPP_VERSION "21b0d0f57a09a7f741d20b7157f440ae87f02c76"
            CACHE STRING "raylib-cpp git tag/branch/sha")
    include(FetchContent)
    FetchContent_Declare(
            raylib_cpp
            GIT_REPOSITORY https://github.com/RobLoach/raylib-cpp.git
            GIT_TAG ${RAYLIB_CPP_VERSION}
    )
    FetchContent_MakeAvailable(raylib_cpp)
endif()

# raygui
FetchContent_Declare(
        raygui
        GIT_REPOSITORY https://github.com/raysan5/raygui.git
        GIT_TAG master
)
FetchContent_MakeAvailable(raygui)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === Output dirs ===
# Above the targets on purpose: these initialise a target's output directory as
# it is created, so setting them further down would leave the binaries in the
# build root instead of build/bin.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# === The game, as a library ===
# Everything except main() lives here, so that the test binary can link the same
# code the game runs. Anything added to the game goes in this list, not in the
# executable below.
add_library(business_game_lib STATIC
        includes/PerlinNoise.hpp
        src/game/Light.cpp
        src/game/Light.hpp
        src/game/Picking.cpp
        src/game/Picking.hpp
        src/game/Transform.cpp
        src/game/Transform.hpp
        src/game/ViewNode.hpp
        src/game/main.hpp
        src/ui/AttachMenu.cpp
        src/ui/AttachMenu.hpp
        src/ui/GridTransformMenu.cpp
        src/ui/GridTransformMenu.hpp
        src/ui/ShaderMenu.cpp
        src/ui/ShaderMenu.hpp
        src/ui/UIButton.cpp
        src/ui/UIButton.hpp
        src/ui/UIImage.cpp
        src/ui/UIImage.hpp
        src/ui/UILabel.cpp
        src/ui/UILabel.hpp
        src/ui/UINode.cpp
        src/ui/UINode.hpp
        src/ui/UINumberRow.cpp
        src/ui/UINumberRow.hpp
        src/ui/UIPrompt.hpp
        src/ui/UIStyle.hpp
        src/ui/UIView.cpp
        src/ui/UIView.hpp
        src/ui/VoxelEditor.cpp
        src/ui/VoxelEditor.hpp
        src/voxel/SingleChunkGrid.cpp
        src/voxel/SingleChunkGrid.hpp
        src/voxel/VoxelFile.cpp
        src/voxel/VoxelFile.hpp
        src/voxel/VoxelGrid.cpp
        src/voxel/VoxelGrid.hpp
        src/voxel/VoxelMap.cpp
        src/voxel/VoxelMap.hpp
        src/voxel/VoxelMesher.cpp
        src/voxel/VoxelMesher.hpp
        src/voxel/VoxelView.cpp
        src/voxel/VoxelView.hpp
)

# PUBLIC, so that both the game and the tests inherit them
target_include_directories(business_game_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/includes
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${raygui_SOURCE_DIR}/src
)
target_link_libraries(business_game_lib PUBLIC raylib raylib_cpp)

# macOS frameworks
if(APPLE)
    target_link_libraries(business_game_lib PUBLIC "-framework IOKit")
    target_link_libraries(business_game_lib PUBLIC "-framework Cocoa")
    target_link_libraries(business_game_lib PUBLIC "-framework OpenGL")
endif()

# === Main executable ===
# Only main() itself: everything it calls comes out of the library.
add_executable(${PROJECT_NAME} src/game/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE business_game_lib)

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

# === Tests ===
# Off for a web build, where there is no test runner to speak of.
if(PLATFORM STREQUAL "Web")
    set(BUSINESS_GAME_BUILD_TESTS_DEFAULT OFF)
else()
    set(BUSINESS_GAME_BUILD_TESTS_DEFAULT ON)
endif()
option(BUSINESS_GAME_BUILD_TESTS "Build the unit tests"
        ${BUSINESS_GAME_BUILD_TESTS_DEFAULT})

if(BUSINESS_GAME_BUILD_TESTS)
    # Catch2 v3 is a compiled library rather than the single header v2 was, so
    # it is fetched and built the same way the other dependencies are. Pinned to
    # a release for the same reason raylib is.
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.16.0
            GIT_SHALLOW 1
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()

    # The tests only cover what can run without a window: the transform maths,
    # the grid hierarchy and attachment, and the file format. Anything that
    # meshes, draws or reads input needs an OpenGL context and is left out.
    add_executable(business_game_tests
            src/tests/TestHelpers.hpp
            src/tests/test_attachment.cpp
            src/tests/test_transform.cpp
            src/tests/test_voxel_file.cpp
    )
    target_link_libraries(business_game_tests PRIVATE
            business_game_lib
            Catch2::Catch2WithMain
    )

    # Registers every TEST_CASE with CTest, so `ctest` in the build directory
    # runs them and names the ones that failed.
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(business_game_tests)
endif()
