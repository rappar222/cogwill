cmake_minimum_required(VERSION 3.22)
project(cogwill VERSION 0.0.1 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(SFML 3 COMPONENTS Graphics Window Audio System QUIET)
if(NOT SFML_FOUND)
    message(STATUS "System SFML 3 not found - automatically downloading SFML 3.0.2...")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build SFML statically" FORCE)
    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.0.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SFML)
endif()

find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "System nlohmann_json not found - automatically downloading nlohmann/json v3.12.0...")
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(cogwill_core STATIC
    src/Catalog.cpp
    src/World.cpp
    src/SaveLoad.cpp
)

target_include_directories(cogwill_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cogwill_core PUBLIC
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(cogwill_core PRIVATE /W4)
else()
    target_compile_options(cogwill_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cogwill
    src/main.cpp
    src/Audio.cpp
    src/Renderer.cpp
    src/UI.cpp
    src/Game.cpp
)

target_link_libraries(cogwill PRIVATE
    cogwill_core
    SFML::Graphics
    SFML::Window
    SFML::Audio
    SFML::System
)

target_compile_definitions(cogwill PRIVATE
    COGWILL_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

if(MSVC)
    target_compile_options(cogwill PRIVATE /W4)
else()
    target_compile_options(cogwill PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET cogwill POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:cogwill>/assets
)

enable_testing()
add_executable(cogwill_tests
    tests/test_simulation.cpp
)
target_link_libraries(cogwill_tests PRIVATE cogwill_core)
if(MSVC)
    target_compile_options(cogwill_tests PRIVATE /W4)
else()
    target_compile_options(cogwill_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME simulation_tests COMMAND cogwill_tests)

install(TARGETS cogwill RUNTIME DESTINATION bin)
install(DIRECTORY assets DESTINATION share/cogwill)
