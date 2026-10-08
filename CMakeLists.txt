cmake_minimum_required(VERSION 3.16)
project(SmartTrafficFlow VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Simulation engine (Core + Entities): plain C++17, no graphics dependency.
# Used by the game, the unit tests and the headless benchmark.
# ---------------------------------------------------------------------------
add_library(traffic_engine STATIC
    src/Core/CityGrid.cpp
    src/Core/Pathfinder.cpp
    src/Core/TrafficSignal.cpp
    src/Entities/Vehicle.cpp
    src/Entities/Lane.cpp
    src/Entities/Intersection.cpp
)
target_include_directories(traffic_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(traffic_engine PRIVATE /W4)
else()
    target_compile_options(traffic_engine PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Unit tests (no SFML needed):  ctest --test-dir build
# ---------------------------------------------------------------------------
enable_testing()
# PlayerController's physics and raycasts are SFML-free, so they are tested too
add_executable(engine_tests tests/test_engine.cpp src/Graphics/PlayerController.cpp)
target_link_libraries(engine_tests PRIVATE traffic_engine)
add_test(NAME engine_tests COMMAND engine_tests)

# ---------------------------------------------------------------------------
# The game (SFML 2.6). Point SFML_DIR at <SFML>/lib/cmake/SFML if CMake
# cannot find it; the bundled tools/ folder is checked automatically.
# ---------------------------------------------------------------------------
set(SFML_STATIC_LIBRARIES FALSE)
if(NOT SFML_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/SFML-2.6.1/lib/cmake/SFML")
    set(SFML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/SFML-2.6.1/lib/cmake/SFML")
endif()
find_package(SFML 2.5 COMPONENTS graphics window system QUIET)

if(SFML_FOUND)
    add_executable(SmartTrafficFlow
        main.cpp
        src/Graphics/Renderer.cpp
        src/Graphics/PlayerController.cpp
        src/Graphics/HUD.cpp
    )
    target_link_libraries(SmartTrafficFlow PRIVATE traffic_engine sfml-graphics sfml-window sfml-system)
    if(WIN32)
        # copy the SFML DLLs next to the executable
        get_filename_component(SFML_ROOT "${SFML_DIR}/../../.." ABSOLUTE)
        file(GLOB SFML_DLLS "${SFML_ROOT}/bin/sfml-graphics-2.dll" "${SFML_ROOT}/bin/sfml-window-2.dll"
                            "${SFML_ROOT}/bin/sfml-system-2.dll")
        foreach(dll ${SFML_DLLS})
            add_custom_command(TARGET SmartTrafficFlow POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" $<TARGET_FILE_DIR:SmartTrafficFlow>)
        endforeach()
        # MinGW runtime DLLs, so the .exe also runs outside this terminal
        if(MINGW)
            get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            foreach(rt libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
                if(EXISTS "${MINGW_BIN}/${rt}")
                    add_custom_command(TARGET SmartTrafficFlow POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN}/${rt}" $<TARGET_FILE_DIR:SmartTrafficFlow>)
                endif()
            endforeach()
        endif()
    endif()
else()
    message(WARNING "SFML 2.6 not found: only the engine and tests will be built. "
                    "Install SFML or pass -DSFML_DIR=<SFML>/lib/cmake/SFML")
endif()
