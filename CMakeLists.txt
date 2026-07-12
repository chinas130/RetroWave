cmake_minimum_required(VERSION 3.20)

project(RetroWave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

set(AUDIO_OUTPUT_SOURCES)
set(UI_PLATFORM_SOURCES)
set(PLATFORM_LIBS)

if(APPLE)
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

add_executable(retrowave
    src/main.cpp
    src/audio/AudioDecoder.cpp
    src/audio/ExternalProcess.cpp
    src/audio/AudioRingBuffer.cpp
    src/audio/AudioStreamDecoder.cpp
    src/audio/WaveformCache.cpp
    src/audio/MetadataCache.cpp
    src/audio/PlaybackEngine.cpp
    src/audio/YtDlpResolver.cpp
    src/core/Lyrics.cpp
    src/core/Playlist.cpp
    src/core/Settings.cpp
    src/ui/TerminalUI.cpp
    ${AUDIO_OUTPUT_SOURCES}
    ${UI_PLATFORM_SOURCES}
)

target_include_directories(retrowave PRIVATE src)

target_link_libraries(retrowave
    PRIVATE
        PkgConfig::FFMPEG
        PkgConfig::NCURSESW
        ${PLATFORM_LIBS}
)

target_compile_options(retrowave PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(lyrics_parser_test
        tests/lyrics_parser_test.cpp
        src/core/Lyrics.cpp
    )
    target_include_directories(lyrics_parser_test PRIVATE src)
    target_compile_options(lyrics_parser_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME lyrics_parser_test COMMAND lyrics_parser_test)

    add_executable(settings_store_test
        tests/settings_store_test.cpp
        src/core/Settings.cpp
    )
    target_include_directories(settings_store_test PRIVATE src)
    target_compile_options(settings_store_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME settings_store_test COMMAND settings_store_test)

    add_executable(playlist_test
        tests/playlist_test.cpp
        src/core/Playlist.cpp
    )
    target_include_directories(playlist_test PRIVATE src)
    target_link_libraries(playlist_test PRIVATE PkgConfig::FFMPEG)
    target_compile_options(playlist_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME playlist_test COMMAND playlist_test)

    add_executable(seek_test
        tests/seek_test.cpp
        src/audio/AudioRingBuffer.cpp
        src/audio/AudioStreamDecoder.cpp
        src/audio/AudioDecoder.cpp
        src/audio/ExternalProcess.cpp
        src/audio/MetadataCache.cpp
        src/audio/WaveformCache.cpp
        src/audio/PlaybackEngine.cpp
        src/audio/YtDlpResolver.cpp
        src/core/Playlist.cpp
        src/core/Lyrics.cpp
        src/core/Settings.cpp
        ${AUDIO_OUTPUT_SOURCES}
    )
    target_include_directories(seek_test PRIVATE src)
    target_link_libraries(seek_test PRIVATE PkgConfig::FFMPEG ${PLATFORM_LIBS})
    target_compile_options(seek_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME seek_test COMMAND seek_test)

    add_executable(playback_engine_test
        tests/playback_engine_test.cpp
        src/audio/AudioRingBuffer.cpp
        src/audio/AudioStreamDecoder.cpp
        src/audio/AudioDecoder.cpp
        src/audio/ExternalProcess.cpp
        src/audio/MetadataCache.cpp
        src/audio/WaveformCache.cpp
        src/audio/PlaybackEngine.cpp
        src/audio/YtDlpResolver.cpp
        src/core/Playlist.cpp
        src/core/Lyrics.cpp
        src/core/Settings.cpp
        ${AUDIO_OUTPUT_SOURCES}
    )
    target_include_directories(playback_engine_test PRIVATE src)
    target_link_libraries(playback_engine_test PRIVATE PkgConfig::FFMPEG ${PLATFORM_LIBS})
    target_compile_options(playback_engine_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME playback_engine_test COMMAND playback_engine_test)
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
