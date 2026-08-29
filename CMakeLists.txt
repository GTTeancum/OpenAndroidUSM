cmake_minimum_required(VERSION 3.25)

project(OpenAndroidUSM VERSION 0.1.0 LANGUAGES CXX)

option(USM_BUILD_TESTS "Build unit tests" ON)

add_executable(OpenAndroidUSM WIN32
    src/app/Main.cpp
    src/app/Application.cpp
    src/platform/windows/Window.cpp
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
    d3d11
    dxgi
    dxguid
    d3dcompiler
    xaudio2
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
    add_test(NAME OpenAndroidUSM.CoreTests COMMAND OpenAndroidUSM.CoreTests)
endif()

