#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace retrowave {

struct PlaylistItem {
    std::filesystem::path path;
    std::string title;
};

class Playlist {
  public:
    static Playlist fromSources(const std::vector<std::string>& sources);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const PlaylistItem& at(std::size_t index) const;
    [[nodiscard]] const std::vector<PlaylistItem>& items() const noexcept;

  private:
    std::vector<PlaylistItem> items_;
};

}  // namespace retrowave
