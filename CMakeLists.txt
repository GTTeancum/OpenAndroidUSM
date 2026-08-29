cmake_minimum_required(VERSION 3.25)

project(OpenAndroidUSM VERSION 0.1.0 LANGUAGES CXX)

option(USM_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zlib)

add_library(OpenAndroidUSM.Core STATIC
    src/filesystem/GbmpArchive.cpp
    src/reconstructed/input/XperiaKeyRouter.cpp
)
target_compile_features(OpenAndroidUSM.Core PUBLIC cxx_std_20)
target_include_directories(OpenAndroidUSM.Core PUBLIC src)
target_link_libraries(OpenAndroidUSM.Core PRIVATE zlibstatic)
if(MSVC)
    target_compile_options(OpenAndroidUSM.Core PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(OpenAndroidUSM WIN32
    src/app/Main.cpp
    src/app/Application.cpp
    src/platform/windows/Window.cpp
    src/platform/windows/XInputController.cpp
    src/renderer/d3d11/D3D11Renderer.cpp
    src/audio/xaudio2/XAudio2System.cpp
)

target_compile_features(OpenAndroidUSM PRIVATE cxx_std_20)
target_compile_definitions(OpenAndroidUSM PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_include_directories(OpenAndroidUSM PRIVATE src)
target_link_libraries(OpenAndroidUSM PRIVATE
    OpenAndroidUSM.Core
    d3d11
    dxgi
    dxguid
    d3dcompiler
    xaudio2
    xinput9_1_0
)

if(MSVC)
    target_compile_options(OpenAndroidUSM PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

include(CTest)
if(BUILD_TESTING AND USM_BUILD_TESTS)
    add_executable(OpenAndroidUSM.CoreTests
        tests/SmokeTests.cpp
    )
    target_compile_features(OpenAndroidUSM.CoreTests PRIVATE cxx_std_20)
    target_include_directories(OpenAndroidUSM.CoreTests PRIVATE src)
    target_link_libraries(OpenAndroidUSM.CoreTests PRIVATE OpenAndroidUSM.Core zlibstatic)
    target_compile_definitions(OpenAndroidUSM.CoreTests PRIVATE
        USM_TEST_GAME_DATA_ROOT="${CMAKE_SOURCE_DIR}/game/data/gameloft/games/spiderman"
    )
    add_test(NAME OpenAndroidUSM.CoreTests COMMAND OpenAndroidUSM.CoreTests)
endif()
