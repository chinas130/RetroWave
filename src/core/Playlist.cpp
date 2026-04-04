#include "core/Playlist.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>

namespace retrowave {
namespace {

std::string normalizeExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return ext;
}

bool isSupportedMediaFile(const std::filesystem::path& path) {
    static const std::set<std::string> kExtensions = {
        ".aac", ".aiff", ".alac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wma"
    };

    return std::filesystem::is_regular_file(path) && kExtensions.contains(normalizeExtension(path));
}

PlaylistItem makeItem(const std::filesystem::path& path) {
    return PlaylistItem{path, path.stem().string()};
}

void appendSource(std::vector<PlaylistItem>& output, const std::filesystem::path& source) {
    if (!std::filesystem::exists(source)) {
        return;
    }

    if (std::filesystem::is_regular_file(source) && isSupportedMediaFile(source)) {
        output.push_back(makeItem(source));
        return;
    }

    if (!std::filesystem::is_directory(source)) {
        return;
    }

    std::vector<std::filesystem::path> discovered;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        if (entry.is_regular_file() && isSupportedMediaFile(entry.path())) {
            discovered.push_back(entry.path());
        }
    }

    std::sort(discovered.begin(), discovered.end());
    for (const auto& path : discovered) {
        output.push_back(makeItem(path));
    }
}

}  // namespace

Playlist Playlist::fromSources(const std::vector<std::string>& sources) {
    Playlist playlist;

    if (sources.empty()) {
        appendSource(playlist.items_, std::filesystem::current_path());
    } else {
        for (const auto& source : sources) {
            appendSource(playlist.items_, std::filesystem::path(source));
        }
    }

    if (playlist.items_.empty()) {
        throw std::runtime_error("No supported audio files were found.");
    }

    return playlist;
}

bool Playlist::empty() const noexcept {
    return items_.empty();
}

std::size_t Playlist::size() const noexcept {
    return items_.size();
}

const PlaylistItem& Playlist::at(std::size_t index) const {
    return items_.at(index);
}

const std::vector<PlaylistItem>& Playlist::items() const noexcept {
    return items_;
}

}  // namespace retrowave
