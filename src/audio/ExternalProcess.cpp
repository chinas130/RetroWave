#include "audio/ExternalProcess.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace retrowave {
namespace {

bool waitForProcess(pid_t pid) noexcept {
    if (pid <= 0) {
        return true;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void signalProcessGroup(pid_t pid, int signal) noexcept {
    if (pid <= 0) {
        return;
    }

    if (kill(-pid, signal) != 0) {
        kill(pid, signal);
    }
}

void terminateProcessGroup(pid_t pid) noexcept {
    if (pid <= 0) {
        return;
    }

    signalProcessGroup(pid, SIGTERM);

    int status = 0;
    for (int attempt = 0; attempt < 25; ++attempt) {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return;
        }
        if (result < 0 && errno != EINTR) {
            return;
        }
        usleep(20'000);
    }

    signalProcessGroup(pid, SIGKILL);
    waitForProcess(pid);
}

}  // namespace

ExternalProcess::~ExternalProcess() {
    terminate();
}

bool ExternalProcess::startStdout(const std::vector<std::string>& args) {
    terminate();
    if (args.empty()) {
        return false;
    }

    int outputPipe[2] = {-1, -1};
    if (pipe(outputPipe) != 0) {
        return false;
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(outputPipe[0]);
        close(outputPipe[1]);
        return false;
    }

    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
    posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(outputPipe[0]);
        close(outputPipe[1]);
        return false;
    }
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawnResult = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    close(outputPipe[1]);
    if (spawnResult != 0) {
        close(outputPipe[0]);
        return false;
    }

    FILE* stream = fdopen(outputPipe[0], "r");
    if (stream == nullptr) {
        close(outputPipe[0]);
        terminateProcessGroup(pid);
        return false;
    }

    stdout_ = stream;
    pid_ = pid;
    return true;
}

std::string ExternalProcess::captureStdout(const std::vector<std::string>& args) {
    ExternalProcess process;
    if (!process.startStdout(args)) {
        return {};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), process.stdoutStream()) != nullptr) {
        output += buffer.data();
    }
    process.closeStdout();

    return process.wait() ? output : std::string{};
}

FILE* ExternalProcess::stdoutStream() const noexcept {
    return stdout_;
}

bool ExternalProcess::active() const noexcept {
    return pid_ > 0 || stdout_ != nullptr;
}

void ExternalProcess::requestStop() noexcept {
    signalProcessGroup(pid_, SIGTERM);
    signalProcessGroup(pid_, SIGKILL);
}

void ExternalProcess::terminate() noexcept {
    const pid_t pid = pid_;
    if (pid > 0) {
        signalProcessGroup(pid, SIGTERM);
    }
    closeStdout();
    terminateProcessGroup(pid);
    pid_ = -1;
}

bool ExternalProcess::wait() noexcept {
    closeStdout();
    const pid_t pid = pid_;
    pid_ = -1;
    return waitForProcess(pid);
}

void ExternalProcess::closeStdout() noexcept {
    if (stdout_ != nullptr) {
        fclose(stdout_);
        stdout_ = nullptr;
    }
}

}  // namespace retrowave
