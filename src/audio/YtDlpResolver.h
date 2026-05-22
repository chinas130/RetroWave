#pragma once

#include <string>

namespace retrowave {

struct YtDlpMedia {
    std::string url;
    double durationSeconds = 0.0;
};

class YtDlpResolver {
  public:
    [[nodiscard]] static bool isYouTubeUrl(const std::string& input);
    [[nodiscard]] static YtDlpMedia resolve(const std::string& input);
};

}  // namespace retrowave
