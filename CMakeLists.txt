cmake_minimum_required(VERSION 3.20)

project(RetroWave VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be used together")
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
include(GNUInstallDirs)
include(CTest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

set(AUDIO_OUTPUT_SOURCES)
set(UI_PLATFORM_SOURCES)
set(PLATFORM_LIBS)

if(APPLE)
    enable_language(OBJCXX)
    list(APPEND AUDIO_OUTPUT_SOURCES src/audio/AudioOutput_mac.cpp)
    list(APPEND UI_PLATFORM_SOURCES src/ui/MediaSession_mac.mm)
    list(APPEND PLATFORM_LIBS "-framework AudioToolbox" "-framework CoreFoundation" "-framework Foundation" "-framework MediaPlayer" "-framework AppKit")
elseif(UNIX AND NOT APPLE)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
    list(APPEND AUDIO_OUTPUT_SOURCES src/audio/AudioOutput_linux.cpp)
    list(APPEND UI_PLATFORM_SOURCES src/ui/MediaSession_linux.cpp)
    list(APPEND PLATFORM_LIBS PkgConfig::ALSA)
else()
    message(FATAL_ERROR "RetroWave currently supports audio output only on Apple and Linux.")
endif()

# ==============================================================================
# LIBRARIES
# ==============================================================================

# Core Library
add_library(retrowave_core STATIC
    src/core/Lyrics.cpp
    src/core/Playlist.cpp
    src/core/TrackQueue.cpp
    src/core/Settings.cpp
    src/core/IpcServer.cpp
    src/core/EventHooks.cpp
)
target_include_directories(retrowave_core PUBLIC src)
target_link_libraries(retrowave_core PUBLIC tomlplusplus::tomlplusplus PkgConfig::FFMPEG)
target_compile_options(retrowave_core PRIVATE -Wall -Wextra -Wpedantic)

# Audio Library
add_library(retrowave_audio STATIC
    src/audio/AudioDecoder.cpp
    src/audio/ExternalProcess.cpp
    src/audio/AudioRingBuffer.cpp
    src/audio/AudioStreamDecoder.cpp
    src/audio/WaveformCache.cpp
    src/audio/MetadataCache.cpp
    src/audio/PlaybackEngine.cpp
    src/audio/YtDlpResolver.cpp
    ${AUDIO_OUTPUT_SOURCES}
)
target_include_directories(retrowave_audio PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(retrowave_audio PUBLIC PkgConfig::FFMPEG Threads::Threads ${PLATFORM_LIBS} retrowave_core)
target_compile_options(retrowave_audio PRIVATE -Wall -Wextra -Wpedantic)

# UI Library
add_library(retrowave_ui STATIC
    src/ui/LayoutEngine.cpp
    src/ui/views/CoverArtView.cpp
    src/ui/views/VisualizerView.cpp
    src/ui/views/LyricsView.cpp
    src/ui/views/ModalsView.cpp
    src/ui/views/ViewUtils.cpp
    src/ui/TerminalUI.cpp
    src/ui/KeyBinder.cpp
    src/ui/Theme.cpp
    ${UI_PLATFORM_SOURCES}
)
target_include_directories(retrowave_ui PUBLIC src)
target_link_libraries(retrowave_ui PUBLIC PkgConfig::NCURSESW tomlplusplus::tomlplusplus retrowave_core retrowave_audio ${PLATFORM_LIBS})
target_compile_options(retrowave_ui PRIVATE -Wall -Wextra -Wpedantic)

# ==============================================================================
# EXECUTABLE
# ==============================================================================

add_executable(retrowave src/main.cpp)
target_link_libraries(retrowave PRIVATE retrowave_ui)
target_compile_options(retrowave PRIVATE -Wall -Wextra -Wpedantic)

# ==============================================================================
# TESTS
# ==============================================================================

if(BUILD_TESTING)
    macro(add_retrowave_test TEST_NAME)
        add_executable(${TEST_NAME} tests/${TEST_NAME}.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE retrowave_ui)
        target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endmacro()

    add_retrowave_test(lyrics_parser_test)
    add_retrowave_test(settings_store_test)
    add_retrowave_test(playlist_test)
    add_retrowave_test(seek_test)
    add_retrowave_test(playback_engine_test)
    add_retrowave_test(ring_buffer_stress_test)
    add_retrowave_test(audio_pipeline_edge_test)
    add_retrowave_test(key_binder_test)
    add_retrowave_test(theme_test)
    add_retrowave_test(ipc_test)
endif()

install(TARGETS retrowave
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES man/man1/retrowave.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install(FILES LICENSE.txt NOTICE README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
