cmake_minimum_required(VERSION 3.20)

project(RetroWave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

add_executable(retrowave
    src/main.cpp
    src/audio/AudioDecoder.cpp
    src/audio/AudioOutput.cpp
    src/audio/AudioRingBuffer.cpp
    src/audio/AudioStreamDecoder.cpp
    src/audio/WaveformCache.cpp
    src/audio/PlaybackEngine.cpp
    src/core/Lyrics.cpp
    src/core/Playlist.cpp
    src/ui/TerminalUI.cpp
)

target_include_directories(retrowave PRIVATE src)

target_link_libraries(retrowave
    PRIVATE
        PkgConfig::FFMPEG
        PkgConfig::NCURSESW
        "-framework AudioToolbox"
        "-framework CoreFoundation"
)

target_compile_options(retrowave PRIVATE -Wall -Wextra -Wpedantic)
