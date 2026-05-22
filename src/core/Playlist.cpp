#include "core/Playlist.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <utility>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/dict.h>
}

namespace retrowave {
namespace {

constexpr std::size_t kMaxRemotePlaylistBytes = 1024 * 1024;

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

bool isPlaylistFile(const std::filesystem::path& path) {
    static const std::set<std::string> kExtensions = {".m3u", ".m3u8", ".pls"};
    return std::filesystem::is_regular_file(path) && kExtensions.contains(normalizeExtension(path));
}

bool isRemoteUrl(std::string_view value) {
    const auto separator = value.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return false;
    }

    std::string scheme(value.substr(0, separator));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });

    static const std::set<std::string> kSchemes = {
        "http", "https", "ftp", "sftp", "rtmp", "rtsp", "icy", "mms", "mmsh"
    };
    return kSchemes.contains(scheme);
}

std::string remoteUrlPath(std::string_view url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return {};
    }

    const auto pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string_view::npos) {
        return {};
    }

    const auto queryStart = url.find_first_of("?#", pathStart);
    const auto length = queryStart == std::string_view::npos ? std::string_view::npos : queryStart - pathStart;
    return std::string(url.substr(pathStart, length));
}

bool isRemotePlaylistUrl(std::string_view url) {
    const auto path = remoteUrlPath(url);
    if (path.empty()) {
        return false;
    }
    const auto extension = normalizeExtension(std::filesystem::path(path));
    return extension == ".m3u" || extension == ".m3u8" || extension == ".pls";
}

std::string remoteBaseUrl(std::string_view url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return {};
    }

    const auto pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string_view::npos) {
        return std::string(url) + "/";
    }

    const auto lastSlash = url.rfind('/', url.find_first_of("?#", pathStart));
    if (lastSlash == std::string_view::npos || lastSlash < schemeEnd + 3) {
        return std::string(url.substr(0, pathStart + 1));
    }
    return std::string(url.substr(0, lastSlash + 1));
}

std::string remoteOrigin(std::string_view url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return {};
    }

    const auto pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string_view::npos) {
        return std::string(url);
    }
    return std::string(url.substr(0, pathStart));
}

std::string resolveRemotePlaylistEntry(const std::string& baseUrl, const std::string& entry) {
    if (isRemoteUrl(entry)) {
        return entry;
    }
    if (entry.empty()) {
        return {};
    }
    if (entry.front() == '/') {
        return remoteOrigin(baseUrl) + entry;
    }
    return remoteBaseUrl(baseUrl) + entry;
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char symbol) { return !std::isspace(symbol); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string parseExtinfTitle(const std::string& line) {
    const auto comma = line.find(',');
    if (comma == std::string::npos || comma + 1 >= line.size()) {
        return {};
    }
    return trim(line.substr(comma + 1));
}

bool looksLikeHlsPlaylist(const std::string& playlistText) {
    std::istringstream input(playlistText);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
        if (line.starts_with("#ext-x-")) {
            return true;
        }
    }
    return false;
}

bool lineLooksLikeHlsTag(const std::string& line) {
    std::string normalized = trim(line);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });
    return normalized.starts_with("#ext-x-");
}

bool openRemoteIo(const std::string& url, AVIOContext** io) {
    AVDictionary* options = nullptr;
    av_dict_set(&options, "timeout", "7000000", 0);
    av_dict_set(&options, "user_agent", "RetroWave/0.1", 0);

    const int openResult = avio_open2(io, url.c_str(), AVIO_FLAG_READ, nullptr, &options);
    av_dict_free(&options);
    return openResult >= 0 && *io != nullptr;
}

std::uint32_t appendSourceRoot(std::vector<std::filesystem::path>& roots, const std::filesystem::path& root) {
    roots.push_back(root);
    return static_cast<std::uint32_t>(roots.size() - 1);
}

PlaylistItem makeDirectFileItem(std::uint32_t sourceIndex) {
    return PlaylistItem{sourceIndex, 0, 0, 0, 0, false};
}

PlaylistItem appendRelativeItem(
    std::string& pathStorage,
    std::uint32_t sourceIndex,
    const std::filesystem::path& relativePath) {
    const std::string encoded = relativePath.generic_string();
    const auto offset = static_cast<std::uint32_t>(pathStorage.size());
    pathStorage += encoded;
    return PlaylistItem{sourceIndex, offset, static_cast<std::uint32_t>(encoded.size()), 0, 0, false};
}

void appendRemoteItem(
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::string& url,
    const std::string& title = {}) {
    const auto offset = static_cast<std::uint32_t>(pathStorage.size());
    pathStorage += url;

    auto titleOffset = static_cast<std::uint32_t>(0);
    auto titleLength = static_cast<std::uint32_t>(0);
    if (!title.empty()) {
        titleOffset = static_cast<std::uint32_t>(pathStorage.size());
        pathStorage += title;
        titleLength = static_cast<std::uint32_t>(title.size());
    }

    items.push_back(PlaylistItem{0, offset, static_cast<std::uint32_t>(url.size()), titleOffset, titleLength, true});
}

void appendSource(
    std::vector<std::filesystem::path>& roots,
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::filesystem::path& source);

void appendPlaylistFile(
    std::vector<std::filesystem::path>& roots,
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::filesystem::path& playlistPath) {
    std::ifstream input(playlistPath);
    if (!input) {
        return;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string playlistText = contents.str();
    if (looksLikeHlsPlaylist(playlistText)) {
        const auto sourceIndex = appendSourceRoot(roots, playlistPath);
        items.push_back(makeDirectFileItem(sourceIndex));
        return;
    }

    const auto baseDirectory = playlistPath.parent_path();
    const bool pls = normalizeExtension(playlistPath) == ".pls";
    std::string pendingTitle;
    std::unordered_map<std::string, std::string> plsTitles;

    std::istringstream playlistInput(playlistText);
    std::string line;
    while (std::getline(playlistInput, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (pls) {
            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                continue;
            }

            std::string key = line.substr(0, separator);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char symbol) {
                return static_cast<char>(std::tolower(symbol));
            });
            if (key.starts_with("title")) {
                plsTitles[key.substr(5)] = trim(line.substr(separator + 1));
                continue;
            }
            if (!key.starts_with("file")) {
                continue;
            }
            const auto itemTitle = plsTitles[key.substr(4)];
            line = trim(line.substr(separator + 1));
            if (isRemoteUrl(line)) {
                appendRemoteItem(pathStorage, items, line, itemTitle);
                continue;
            }

            const auto localPath = std::filesystem::path(line).is_absolute()
                ? std::filesystem::path(line)
                : baseDirectory / line;
            appendSource(roots, pathStorage, items, localPath);
            continue;
        } else if (line.front() == '#') {
            if (line.starts_with("#EXTINF:")) {
                pendingTitle = parseExtinfTitle(line);
                continue;
            }
            continue;
        }

        if (isRemoteUrl(line)) {
            appendRemoteItem(pathStorage, items, line, pendingTitle);
            pendingTitle.clear();
            continue;
        }

        const auto localPath = std::filesystem::path(line).is_absolute()
            ? std::filesystem::path(line)
            : baseDirectory / line;
        appendSource(roots, pathStorage, items, localPath);
        pendingTitle.clear();
    }
}

void processRemotePlaylistLine(
    std::vector<std::filesystem::path>& roots,
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::string& baseUrl,
    bool pls,
    std::string& pendingTitle,
    std::unordered_map<std::string, std::string>& plsTitles,
    const std::string& rawLine) {
    std::string line = trim(rawLine);
    if (line.empty()) {
        return;
    }

    if (pls) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            return;
        }

        std::string key = line.substr(0, separator);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
        if (key.starts_with("title")) {
            plsTitles[key.substr(5)] = trim(line.substr(separator + 1));
            return;
        }
        if (!key.starts_with("file")) {
            return;
        }
        pendingTitle = plsTitles[key.substr(4)];
        line = trim(line.substr(separator + 1));
    } else if (line.front() == '#') {
        if (line.starts_with("#EXTINF:")) {
            pendingTitle = parseExtinfTitle(line);
        }
        return;
    }

    const auto resolved = resolveRemotePlaylistEntry(baseUrl, line);
    if (resolved.empty()) {
        return;
    }

    if (isRemoteUrl(resolved)) {
        appendRemoteItem(pathStorage, items, resolved, pendingTitle);
        pendingTitle.clear();
        return;
    }

    appendSource(roots, pathStorage, items, std::filesystem::path(resolved));
    pendingTitle.clear();
}

void appendRemotePlaylist(
    std::vector<std::filesystem::path>& roots,
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::string& url) {
    AVIOContext* io = nullptr;
    if (!openRemoteIo(url, &io)) {
        appendRemoteItem(pathStorage, items, url);
        return;
    }

    const bool pls = normalizeExtension(std::filesystem::path(remoteUrlPath(url))) == ".pls";
    const auto before = items.size();
    std::string pendingTitle;
    std::unordered_map<std::string, std::string> plsTitles;
    std::string carry;
    carry.reserve(4096);
    std::array<unsigned char, 4096> buffer{};
    std::size_t bytesRead = 0;
    bool hlsPlaylist = false;
    int inspectedLines = 0;

    while (bytesRead < kMaxRemotePlaylistBytes) {
        const int chunkBytes = avio_read(io, buffer.data(), static_cast<int>(buffer.size()));
        if (chunkBytes <= 0) {
            break;
        }
        bytesRead += static_cast<std::size_t>(chunkBytes);
        carry.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(chunkBytes));

        std::size_t lineStart = 0;
        while (lineStart < carry.size()) {
            const auto lineEnd = carry.find('\n', lineStart);
            const bool hasLine = lineEnd != std::string::npos;
            const auto lineStop = hasLine ? lineEnd : carry.size();
            std::string line = carry.substr(lineStart, lineStop - lineStart);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (hasLine) {
                if (inspectedLines < 64 && lineLooksLikeHlsTag(line)) {
                    hlsPlaylist = true;
                }
                ++inspectedLines;
                processRemotePlaylistLine(roots, pathStorage, items, url, pls, pendingTitle, plsTitles, line);
                lineStart = lineEnd + 1;
            } else {
                carry.erase(0, lineStart);
                break;
            }
        }

        if (hlsPlaylist) {
            break;
        }
    }

    if (!carry.empty() && !hlsPlaylist) {
        if (inspectedLines < 64 && lineLooksLikeHlsTag(carry)) {
            hlsPlaylist = true;
        } else {
            processRemotePlaylistLine(roots, pathStorage, items, url, pls, pendingTitle, plsTitles, carry);
        }
    }

    avio_closep(&io);

    if (hlsPlaylist) {
        appendRemoteItem(pathStorage, items, url);
        return;
    }

    if (items.size() == before) {
        appendRemoteItem(pathStorage, items, url);
    }
}

void appendSource(
    std::vector<std::filesystem::path>& roots,
    std::string& pathStorage,
    std::vector<PlaylistItem>& items,
    const std::filesystem::path& source) {
    const auto sourceString = source.string();
    if (isRemoteUrl(sourceString)) {
        if (isRemotePlaylistUrl(sourceString)) {
            appendRemotePlaylist(roots, pathStorage, items, sourceString);
            return;
        }
        appendRemoteItem(pathStorage, items, sourceString);
        return;
    }

    if (!std::filesystem::exists(source)) {
        return;
    }

    if (isPlaylistFile(source)) {
        appendPlaylistFile(roots, pathStorage, items, source);
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
    if (item.remote) {
        return std::filesystem::path(std::string(relativePathView(item)));
    }

    const auto& source = sources_.at(item.sourceIndex);
    if (item.pathLength == 0) {
        return source;
    }
    return source / std::filesystem::path(std::string(relativePathView(item)));
}

std::string Playlist::sourceAt(std::size_t index) const {
    const auto& item = items_.at(index);
    if (item.remote) {
        return std::string(relativePathView(item));
    }
    return pathAt(index).string();
}

std::string Playlist::titleAt(std::size_t index) const {
    const auto& item = items_.at(index);
    if (item.remote) {
        if (item.titleLength > 0) {
            return std::string(std::string_view(pathStorage_).substr(item.titleOffset, item.titleLength));
        }

        const std::string url = std::string(relativePathView(item));
        const auto withoutQuery = url.substr(0, url.find_first_of("?#"));
        const auto slash = withoutQuery.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < withoutQuery.size()) {
            return withoutQuery.substr(slash + 1);
        }
        return url;
    }

    if (item.pathLength == 0) {
        return sources_.at(item.sourceIndex).stem().string();
    }
    return std::filesystem::path(std::string(relativePathView(item))).stem().string();
}

bool Playlist::isRemoteAt(std::size_t index) const {
    return items_.at(index).remote;
}

std::string_view Playlist::relativePathView(const PlaylistItem& item) const noexcept {
    return std::string_view(pathStorage_).substr(item.pathOffset, item.pathLength);
}

}  // namespace retrowave
