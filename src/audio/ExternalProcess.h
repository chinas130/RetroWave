#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include <sys/types.h>

namespace retrowave {

class ExternalProcess {
  public:
    ExternalProcess() = default;
    ~ExternalProcess();

    ExternalProcess(const ExternalProcess&) = delete;
    ExternalProcess& operator=(const ExternalProcess&) = delete;
    ExternalProcess(ExternalProcess&&) = delete;
    ExternalProcess& operator=(ExternalProcess&&) = delete;

    [[nodiscard]] bool startStdout(const std::vector<std::string>& args);
    [[nodiscard]] static std::string captureStdout(const std::vector<std::string>& args);

    [[nodiscard]] FILE* stdoutStream() const noexcept;
    [[nodiscard]] bool active() const noexcept;

    void requestStop() noexcept;
    void terminate() noexcept;
    [[nodiscard]] bool wait() noexcept;

  private:
    void closeStdout() noexcept;

    FILE* stdout_ = nullptr;
    pid_t pid_ = -1;
};

}  // namespace retrowave
