#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <string>
#include <vector>

namespace retrowave {

struct PlaylistItem {
    std::uint32_t sourceIndex = 0;
    std::uint32_t pathOffset = 0;
    std::uint32_t pathLength = 0;
};

class Playlist {
  public:
    static Playlist fromSources(const std::vector<std::string>& sources);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const PlaylistItem& at(std::size_t index) const;
    [[nodiscard]] std::filesystem::path pathAt(std::size_t index) const;
    [[nodiscard]] std::string titleAt(std::size_t index) const;

  private:
    [[nodiscard]] std::string_view relativePathView(const PlaylistItem& item) const noexcept;

    std::vector<std::filesystem::path> sources_;
    std::string pathStorage_;
    std::vector<PlaylistItem> items_;
};

}  // namespace retrowave
