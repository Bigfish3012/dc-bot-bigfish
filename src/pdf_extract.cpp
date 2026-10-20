#include <string>
#include <iostream>
#include "../include/runtime_limits.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {
struct Descriptor {
    int value = -1;
    explicit Descriptor(int fd = -1) : value(fd) {}
    ~Descriptor() { if (value >= 0) close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
};
struct TemporaryPdf {
    char path[32] = "/tmp/dc-bot-pdf-XXXXXX";
    Descriptor fd{mkstemp(path)};
    ~TemporaryPdf() { if (fd.value >= 0) unlink(path); }
};
struct Child {
    pid_t pid = -1;
    ~Child() {
        if (pid > 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
};
struct SpawnActions {
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    ~SpawnActions() { if (!error) posix_spawn_file_actions_destroy(&actions); }
};
}

std::string extract_text_from_pdf(const std::string& content) {
    if (content.empty() || content.size() > limits::attachment_bytes) return {};
    TemporaryPdf temporary;
    if (temporary.fd.value < 0 ||
        fcntl(temporary.fd.value, F_SETFD, FD_CLOEXEC) < 0) return {};
    std::size_t written = 0;
    while (written < content.size()) {
        const auto n = write(temporary.fd.value, content.data() + written, content.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return {};
        written += static_cast<std::size_t>(n);
    }

    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC) < 0) return {};
    Descriptor output(descriptors[0]);
    Descriptor writer(descriptors[1]);
    SpawnActions actions;
    if (actions.error) return {};
    auto* file_actions = &actions.actions;
    if (posix_spawn_file_actions_adddup2(file_actions, writer.value, STDOUT_FILENO) ||
        posix_spawn_file_actions_addclose(file_actions, output.value) ||
        posix_spawn_file_actions_addclose(file_actions, writer.value) ||
        posix_spawn_file_actions_addopen(file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) ||
        posix_spawn_file_actions_addopen(file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0)) return {};

    // No shell and no user-controlled command arguments. posix_spawn is safe to
    // use after DPP starts threads; a failed spawn leaves no child to reap.
    // prlimit execs pdftotext in the same PID and applies limits before parsing.
    char program[] = "/usr/bin/prlimit";
    const auto memory_limit = "--as=" + std::to_string(limits::pdf_memory_bytes) + ":" +
                              std::to_string(limits::pdf_memory_bytes);
    const auto cpu_limit = "--cpu=" + std::to_string(limits::pdf_cpu_seconds) + ":" +
                           std::to_string(limits::pdf_cpu_seconds);
    char core_limit[] = "--core=0:0";
    char separator[] = "--";
    char parser[] = "/usr/bin/pdftotext";
    char encoding_option[] = "-enc";
    char encoding[] = "UTF-8";
    char destination[] = "-";
    char* arguments[] = {program, const_cast<char*>(memory_limit.c_str()),
        const_cast<char*>(cpu_limit.c_str()), core_limit, separator, parser,
        encoding_option, encoding, temporary.path, destination, nullptr};
    Child child;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limits::pdf_seconds);
    pid_t pid;
    const int spawned = posix_spawn(&pid, program, file_actions, nullptr, arguments, environ);
    if (spawned != 0) {
        std::cerr << "[ERROR] Cannot start PDF resource limiter: " << spawned << '\n';
        return {};
    }
    child.pid = pid;
    close(writer.value);
    writer.value = -1;

    std::string text;
    char buffer[4096];
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "[WARN] PDF extraction timed out\n";
            return {};
        }
        pollfd readable{output.value, POLLIN, 0};
        const int polled = poll(&readable, 1, 100);
        if (polled < 0 && errno == EINTR) continue;
        if (polled < 0 || (readable.revents & (POLLERR | POLLNVAL))) return {};
        if (!polled) continue;
        const auto n = read(output.value, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return {};
        if (!n) break;
        if (static_cast<std::size_t>(n) > limits::input_bytes - text.size()) {
            std::cerr << "[WARN] PDF extracted text exceeds input limit\n";
            return {};
        }
        text.append(buffer, static_cast<std::size_t>(n));
    }
    int status = 0;
    for (;;) {
        const auto result = waitpid(child.pid, &status, WNOHANG);
        if (result == child.pid) { child.pid = -1; break; }
        if (result < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) child.pid = -1;
            return {};
        }
        if (std::chrono::steady_clock::now() >= deadline) return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    return text;
}
