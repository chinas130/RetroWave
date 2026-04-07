#include "audio/PlaybackEngine.h"
#include "core/Playlist.h"
#include "ui/TerminalUI.h"

extern "C" {
#include <libavutil/log.h>
}

#include <clocale>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::setlocale(LC_ALL, "");
        av_log_set_level(AV_LOG_QUIET);

        std::vector<std::string> sources;
        for (int index = 1; index < argc; ++index) {
            sources.emplace_back(argv[index]);
        }

        auto playlist = retrowave::Playlist::fromSources(sources);
        retrowave::PlaybackEngine engine(std::move(playlist));
        retrowave::TerminalUI ui(engine);
        return ui.run();
    } catch (const std::exception& error) {
        std::cerr << "RetroWave: " << error.what() << '\n';
        std::cerr << "Usage: retrowave [music-file-or-directory ...]\n";
        return 1;
    }
}
