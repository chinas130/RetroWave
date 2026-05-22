#include "audio/YtDlpResolver.h"

#include "audio/ExternalProcess.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace retrowave {
namespace {

std::string trimLine(std::string line) {
    const auto notSpace = [](unsigned char symbol) { return !std::isspace(symbol); };
    line.erase(line.begin(), std::find_if(line.begin(), line.end(), notSpace));
    line.erase(std::find_if(line.rbegin(), line.rend(), notSpace).base(), line.end());
    return line;
}

std::vector<std::string> splitNonEmptyLines(const std::string& output) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= output.size()) {
        const auto newline = output.find('\n', start);
        const auto length = newline == std::string::npos ? output.size() - start : newline - start;
        auto line = trimLine(output.substr(start, length));
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

double parsePositiveDouble(const std::string& value) {
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || parsed <= 0.0) {
        return 0.0;
    }
    return parsed;
}

std::string chooseAudioUrl(const std::vector<std::string>& urls) {
    for (const auto& url : urls) {
        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char symbol) {
            return static_cast<char>(std::tolower(symbol));
        });
        if (lower.find("mime=audio") != std::string::npos || lower.find("mime%3daudio") != std::string::npos) {
            return url;
        }
    }

    return urls.empty() ? std::string{} : urls.back();
}

std::string resolveUrl(const std::string& input) {
    const std::string format = "bestaudio[acodec!=none]/best[acodec!=none]/bestaudio/best";
    const auto output = ExternalProcess::captureStdout({
        "yt-dlp",
        "-f",
        format,
        "-g",
        "--no-playlist",
        "--no-warnings",
        input,
    });
    return chooseAudioUrl(splitNonEmptyLines(output));
}

double resolveDuration(const std::string& input) {
    const auto output = ExternalProcess::captureStdout({
        "yt-dlp",
        "--no-playlist",
        "--no-warnings",
        "--print",
        "duration",
        input,
    });
    const auto lines = splitNonEmptyLines(output);
    return lines.empty() ? 0.0 : parsePositiveDouble(lines.front());
}

}  // namespace

bool YtDlpResolver::isYouTubeUrl(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });
    return lower.find("youtube.com/") != std::string::npos ||
        lower.find("youtu.be/") != std::string::npos ||
        lower.find("music.youtube.com/") != std::string::npos;
}

YtDlpMedia YtDlpResolver::resolve(const std::string& input) {
    YtDlpMedia result;
    result.url = resolveUrl(input);
    result.durationSeconds = resolveDuration(input);
    return result;
}

}  // namespace retrowave
