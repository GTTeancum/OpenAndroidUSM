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

set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGIXML_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(pugixml)

function(usm_download_verified url output sha256)
    if(EXISTS "${output}")
        file(SHA256 "${output}" existing_sha256)
        if(existing_sha256 STREQUAL sha256)
            return()
        endif()
    endif()
    file(DOWNLOAD "${url}" "${output}"
        TLS_VERIFY ON
        EXPECTED_HASH "SHA256=${sha256}"
    )
endfunction()

set(POWERVR_DECOMPRESS_DIR "${CMAKE_BINARY_DIR}/_deps/powervr-decompress")
file(MAKE_DIRECTORY "${POWERVR_DECOMPRESS_DIR}")
set(POWERVR_SOURCE_BASE
    "https://raw.githubusercontent.com/powervr-graphics/Native_SDK/2b1bf2f14d3365d0bb801e2a6a131a319d3a2e48/framework/PVRCore/texture"
)
usm_download_verified(
    "${POWERVR_SOURCE_BASE}/PVRTDecompress.cpp"
    "${POWERVR_DECOMPRESS_DIR}/PVRTDecompress.cpp"
    "74559c5a4b8161aafce1ebe984bf8060896feaa81276b614491dbee755d6e4c7"
)
usm_download_verified(
    "${POWERVR_SOURCE_BASE}/PVRTDecompress.h"
    "${POWERVR_DECOMPRESS_DIR}/PVRTDecompress.h"
    "67123a36b99df380af76a65f3dfa3a7f368a153f5401b36a9ff1ed233cee6f5f"
)
add_library(OpenAndroidUSM.PVRTC STATIC
    ${POWERVR_DECOMPRESS_DIR}/PVRTDecompress.cpp
)
target_compile_features(OpenAndroidUSM.PVRTC PUBLIC cxx_std_20)
target_include_directories(OpenAndroidUSM.PVRTC PUBLIC
    ${POWERVR_DECOMPRESS_DIR}
)

set(STB_VORBIS_DIR "${CMAKE_BINARY_DIR}/_deps/stb-vorbis")
file(MAKE_DIRECTORY "${STB_VORBIS_DIR}")
usm_download_verified(
    "https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c"
    "${STB_VORBIS_DIR}/stb_vorbis.c"
    "4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636"
)
set_source_files_properties(${STB_VORBIS_DIR}/stb_vorbis.c PROPERTIES
    LANGUAGE CXX
)
add_library(OpenAndroidUSM.Vorbis STATIC
    ${STB_VORBIS_DIR}/stb_vorbis.c
)
target_compile_features(OpenAndroidUSM.Vorbis PUBLIC cxx_std_20)
target_include_directories(OpenAndroidUSM.Vorbis PUBLIC
    ${STB_VORBIS_DIR}
)
if(MSVC)
    target_compile_options(OpenAndroidUSM.Vorbis PRIVATE /W0 /EHsc)
endif()

add_library(OpenAndroidUSM.Core STATIC
    src/audio/CinematicSoundBank.cpp
    src/audio/EnemyBehaviorSoundBank.cpp
    src/audio/OggAudio.cpp
    src/audio/PlayerStateSoundBank.cpp
    src/audio/SoundEventCatalog.cpp
    src/audio/VoxSoundTable.cpp
    src/assets/BresFile.cpp
    src/assets/BtexTexture.cpp
    src/assets/DdsAtcTexture.cpp
    src/assets/ColladaAnimation.cpp
    src/assets/ColladaMesh.cpp
    src/assets/ColladaSkinning.cpp
    src/assets/IrrScene.cpp
    src/assets/SpriteAtlas.cpp
    src/filesystem/GbmpArchive.cpp
    src/game/CinematicScript.cpp
    src/game/AttackConfig.cpp
    src/game/ButtonConfig.cpp
    src/game/CinematicPlayer.cpp
    src/game/CinematicCamera.cpp
    src/game/CinematicCameraTrack.cpp
    src/game/CinematicUiRuntime.cpp
    src/game/EnemyBehaviorConfig.cpp
    src/game/EnemyAttributeConfig.cpp
    src/game/EnemyAttackIntervalConfig.cpp
    src/game/EnemyRangeAttackConfig.cpp
    src/game/EnemySpecialActionConfig.cpp
    src/game/GameplayCamera.cpp
    src/game/GameplayPlayer.cpp
    src/game/LevelCollision.cpp
    src/game/LevelCinematicRuntime.cpp
    src/game/LevelEnemyRuntime.cpp
    src/game/LevelObjectRuntime.cpp
    src/game/LevelTriggerRuntime.cpp
    src/game/LevelOneBootstrap.cpp
    src/game/LocalizedStringTable.cpp
    src/game/LevelSlideRuntime.cpp
    src/game/PlayerHudHealthState.cpp
    src/game/PlayerStateConfig.cpp
    src/game/QuickTimeEventRuntime.cpp
    src/game/WebGrabPointRuntime.cpp
    src/game/WebSwingRuntime.cpp
    src/reconstructed/input/XperiaKeyRouter.cpp
)
target_compile_features(OpenAndroidUSM.Core PUBLIC cxx_std_20)
target_include_directories(OpenAndroidUSM.Core PUBLIC src)
target_link_libraries(OpenAndroidUSM.Core PRIVATE
    OpenAndroidUSM.PVRTC
    OpenAndroidUSM.Vorbis
    zlibstatic
    pugixml::pugixml
)
if(MSVC)
    target_compile_options(OpenAndroidUSM.Core PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_library(OpenAndroidUSM.D3D11 STATIC
    src/renderer/d3d11/D3D11Renderer.cpp
)
target_compile_features(OpenAndroidUSM.D3D11 PUBLIC cxx_std_20)
target_compile_definitions(OpenAndroidUSM.D3D11 PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_include_directories(OpenAndroidUSM.D3D11 PUBLIC src)
target_link_libraries(OpenAndroidUSM.D3D11 PUBLIC OpenAndroidUSM.Core)
target_link_libraries(OpenAndroidUSM.D3D11 PRIVATE
    d3d11
    dxgi
    dxguid
    d3dcompiler
    gdi32
)
if(MSVC)
    target_compile_options(OpenAndroidUSM.D3D11 PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_executable(OpenAndroidUSM WIN32
    src/app/Main.cpp
    src/app/Application.cpp
    src/platform/windows/GameDataLocator.cpp
    src/platform/windows/Window.cpp
    src/platform/windows/XInputController.cpp
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
    OpenAndroidUSM.D3D11
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

    add_executable(OpenAndroidUSM.RenderTests
        tests/D3D11RenderTests.cpp
    )
    target_compile_features(OpenAndroidUSM.RenderTests PRIVATE cxx_std_20)
    target_compile_definitions(OpenAndroidUSM.RenderTests PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        USM_TEST_GAME_DATA_ROOT="${CMAKE_SOURCE_DIR}/game/data/gameloft/games/spiderman"
    )
    target_include_directories(OpenAndroidUSM.RenderTests PRIVATE src)
    target_link_libraries(OpenAndroidUSM.RenderTests PRIVATE
        OpenAndroidUSM.D3D11
        d3d11
        dxgi
        d3dcompiler
    )
    add_test(NAME OpenAndroidUSM.RenderTests COMMAND OpenAndroidUSM.RenderTests)
endif()
