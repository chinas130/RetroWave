cmake_minimum_required(VERSION 3.20)

project(RetroWave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

set(AUDIO_OUTPUT_SOURCES)
set(PLATFORM_LIBS)

if(APPLE)
    list(APPEND AUDIO_OUTPUT_SOURCES src/audio/AudioOutput_mac.cpp)
    list(APPEND PLATFORM_LIBS "-framework AudioToolbox" "-framework CoreFoundation")
elseif(UNIX AND NOT APPLE)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
    list(APPEND AUDIO_OUTPUT_SOURCES src/audio/AudioOutput_linux.cpp)
    list(APPEND PLATFORM_LIBS PkgConfig::ALSA)
else()
    message(FATAL_ERROR "RetroWave currently supports audio output only on Apple and Linux.")
endif()

add_executable(retrowave
    src/main.cpp
    src/audio/AudioDecoder.cpp
    src/audio/AudioRingBuffer.cpp
    src/audio/AudioStreamDecoder.cpp
    src/audio/WaveformCache.cpp
    src/audio/PlaybackEngine.cpp
    src/core/Lyrics.cpp
    src/core/Playlist.cpp
    src/core/Settings.cpp
    src/ui/TerminalUI.cpp
    ${AUDIO_OUTPUT_SOURCES}
)

target_include_directories(retrowave PRIVATE src)

target_link_libraries(retrowave
    PRIVATE
        PkgConfig::FFMPEG
        PkgConfig::NCURSESW
        ${PLATFORM_LIBS}
)

target_compile_options(retrowave PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS retrowave
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES man/man1/retrowave.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install(FILES LICENSE.txt NOTICE README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
