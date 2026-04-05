#include "core/Playlist.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>
#include <stdexcept>
#include <utility>

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

std::uint32_t appendSourceRoot(std::vector<std::filesystem::path>& roots, const std::filesystem::path& root) {
    roots.push_back(root);
    return static_cast<std::uint32_t>(roots.size() - 1);
}

PlaylistItem makeDirectFileItem(std::uint32_t sourceIndex) {
    return PlaylistItem{sourceIndex, 0, 0};
}

PlaylistItem appendRelativeItem(
    std::string& pathStorage,
    std::uint32_t sourceIndex,
    const std::filesystem::path& relativePath) {
    const std::string encoded = relativePath.generic_string();
    const auto offset = static_cast<std::uint32_t>(pathStorage.size());
    pathStorage += encoded;
    return PlaylistItem{sourceIndex, offset, static_cast<std::uint32_t>(encoded.size())};
}

void appendSource(
    std::vector<std::filesystem::path>& roots,
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::filesystem::path& source) {
    if (!std::filesystem::exists(source)) {
        return;
    }

    if (std::filesystem::is_regular_file(source) && isSupportedMediaFile(source)) {
        const auto sourceIndex = appendSourceRoot(roots, source);
        items.push_back(makeDirectFileItem(sourceIndex));
        return;
    }

    if (!std::filesystem::is_directory(source)) {
        return;
    }

    const auto sourceIndex = appendSourceRoot(roots, source);
    std::vector<std::filesystem::path> discovered;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        if (entry.is_regular_file() && isSupportedMediaFile(entry.path())) {
            discovered.push_back(std::filesystem::relative(entry.path(), source));
        }
    }

    std::sort(discovered.begin(), discovered.end());
    for (const auto& path : discovered) {
        items.push_back(appendRelativeItem(pathStorage, sourceIndex, path));
    }
}

}  // namespace

Playlist Playlist::fromSources(const std::vector<std::string>& sources) {
    Playlist playlist;

    if (sources.empty()) {
        appendSource(playlist.sources_, playlist.pathStorage_, playlist.items_, std::filesystem::current_path());
    } else {
        for (const auto& source : sources) {
            appendSource(playlist.sources_, playlist.pathStorage_, playlist.items_, std::filesystem::path(source));
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

std::filesystem::path Playlist::pathAt(std::size_t index) const {
    const auto& item = items_.at(index);
    const auto& source = sources_.at(item.sourceIndex);
    if (item.pathLength == 0) {
        return source;
    }
    return source / std::filesystem::path(std::string(relativePathView(item)));
}

std::string Playlist::titleAt(std::size_t index) const {
    const auto& item = items_.at(index);
    if (item.pathLength == 0) {
        return sources_.at(item.sourceIndex).stem().string();
    }
    return std::filesystem::path(std::string(relativePathView(item))).stem().string();
}

std::string_view Playlist::relativePathView(const PlaylistItem& item) const noexcept {
    return std::string_view(pathStorage_).substr(item.pathOffset, item.pathLength);
}

}  // namespace retrowave
