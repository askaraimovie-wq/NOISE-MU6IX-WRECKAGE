cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WRECKAGE_VER_STRING)
string(STRIP "${WRECKAGE_VER_STRING}" WRECKAGE_VER_STRING)
string(REPLACE "." ";" VER_LIST "${WRECKAGE_VER_STRING}")
list(GET VER_LIST 0 WRECKAGE_VER_MAJOR)
list(GET VER_LIST 1 WRECKAGE_VER_MINOR)
list(GET VER_LIST 2 WRECKAGE_VER_PATCH)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

project(NoiseMu6ixWreckage VERSION ${WRECKAGE_VER_STRING} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resource/version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(WITH_OGG OFF CACHE BOOL "" FORCE)
set(ENABLE_MULTITHREADING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/flac EXCLUDE_FROM_ALL)

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_PLUGINS_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES ON CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/vst3sdk EXCLUDE_FROM_ALL)

set(WRECKAGE_SOURCES
    src/vst3/wreckage_factory.cpp
    src/vst3/wreckage_processor.cpp
    src/vst3/wreckage_controller.cpp
    src/vst3/wreckage_view_win.cpp
)

function(add_wreckage_vst3 TARGET_NAME IS_PLAYER_FLAG)
    smtg_add_vst3plugin(${TARGET_NAME} ${WRECKAGE_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vst3"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/flac/include"
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        WRECKAGE_IS_PLAYER=${IS_PLAYER_FLAG}
        WRECKAGE_USE_FLAC=1
        FLAC__NO_DLL=1
        UNICODE
        _UNICODE
        NOMINMAX
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        sdk
        FLAC::FLAC
    )
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE
            legacy_stdio_definitions d2d1 dwrite ole32 shell32 comdlg32 uuid shlwapi user32 gdi32
        )
    endif()
endfunction()

add_wreckage_vst3(WRECKAGE 0)
add_wreckage_vst3(WRECKAGE_Player 1)
