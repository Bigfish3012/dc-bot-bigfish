#include <string>
#include "../include/bot_error.h"
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
[[noreturn]] void pdf_error(const std::string& detail,const std::string& reason) {
    throw ChatError(detail,"PDF 处理失败："+reason);
}
struct Descriptor {
    int value = -1;
    explicit Descriptor(int fd = -1) : value(fd) {}
    ~Descriptor() { if (value >= 0) close(value); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
};
struct TemporaryPdf {
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/dc-bot-pdf-XXXXXX";
    Descriptor fd{mkstemp(path.data())};
    ~TemporaryPdf() { if (fd.value >= 0) unlink(path.c_str()); }
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
    if (content.empty()) pdf_error("Empty PDF","文件内容为空。");
    if (content.size()>limits::attachment_bytes) pdf_error("PDF too large","文件超过 2 MiB 限制。");
    if (access("/usr/bin/pdftotext",X_OK)!=0) pdf_error("pdftotext unavailable","系统未安装 pdftotext 或没有执行权限，请管理员检查 poppler-utils。");
    if (access("/usr/bin/prlimit",X_OK)!=0) pdf_error("prlimit unavailable","系统未安装 prlimit 或没有执行权限，请管理员检查 util-linux。");
    TemporaryPdf temporary;
    if (temporary.fd.value < 0 ||
        fcntl(temporary.fd.value, F_SETFD, FD_CLOEXEC) < 0) pdf_error("Cannot create PDF temporary file","无法创建临时文件，请管理员检查临时目录权限和磁盘空间。");
    std::size_t written = 0;
    while (written < content.size()) {
        const auto n = write(temporary.fd.value, content.data() + written, content.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) pdf_error("Cannot write PDF temporary file","临时文件写入失败，可能是磁盘已满或系统文件操作异常。");
        written += static_cast<std::size_t>(n);
    }

    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC) < 0) pdf_error("Cannot create PDF pipe","无法创建解析管道，系统文件句柄或资源不足。");
    Descriptor output(descriptors[0]);
    Descriptor writer(descriptors[1]);
    SpawnActions actions;
    if (actions.error) pdf_error("Cannot initialize PDF spawn actions","无法初始化解析子进程，系统资源不足。");
    auto* file_actions = &actions.actions;
    if (posix_spawn_file_actions_adddup2(file_actions, writer.value, STDOUT_FILENO) ||
        posix_spawn_file_actions_addclose(file_actions, output.value) ||
        posix_spawn_file_actions_addclose(file_actions, writer.value) ||
        posix_spawn_file_actions_addopen(file_actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) ||
        posix_spawn_file_actions_addopen(file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0)) pdf_error("Cannot configure PDF spawn actions","无法配置解析子进程的输入输出。");

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
        encoding_option, encoding, temporary.path.data(), destination, nullptr};
    Child child;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limits::pdf_seconds);
    pid_t pid;
    const int spawned = posix_spawn(&pid, program, file_actions, nullptr, arguments, environ);
    if (spawned != 0) {
        std::cerr << "[ERROR] Cannot start PDF resource limiter: " << spawned << '\n';
        pdf_error("Cannot spawn PDF parser","解析子进程无法启动（系统错误码 "+std::to_string(spawned)+"）。");
    }
    child.pid = pid;
    close(writer.value);
    writer.value = -1;

    std::string text;
    char buffer[4096];
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "[WARN] PDF extraction timed out\n";
            pdf_error("PDF extraction timeout","解析超过 15 秒，请缩小或简化 PDF。");
        }
        pollfd readable{output.value, POLLIN, 0};
        const int polled = poll(&readable, 1, 100);
        if (polled < 0 && errno == EINTR) continue;
        if (polled < 0 || (readable.revents & (POLLERR | POLLNVAL))) pdf_error("PDF pipe polling failed","等待解析结果时系统管道发生错误。");
        if (!polled) continue;
        const auto n = read(output.value, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) pdf_error("PDF pipe read failed","读取解析结果时系统管道发生错误。");
        if (!n) break;
        if (static_cast<std::size_t>(n) > limits::input_bytes - text.size()) {
            std::cerr << "[WARN] PDF extracted text exceeds input limit\n";
            pdf_error("PDF text limit exceeded","提取的文字超过 64 KiB，请拆分 PDF。");
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
            pdf_error("Cannot collect PDF child status","无法取得解析子进程的退出状态。");
        }
        if (std::chrono::steady_clock::now() >= deadline) pdf_error("PDF parser exit timeout","解析子进程超过 15 秒仍未结束。");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status)) pdf_error("PDF parser terminated by signal","解析进程被终止，可能触发 CPU/内存保护，请简化 PDF。");
    if (WEXITSTATUS(status)!=0) pdf_error("PDF parser exited with error","解析工具返回错误码 "+std::to_string(WEXITSTATUS(status))+"，文件可能损坏、加密或不允许提取文字。");
    if (text.empty()) pdf_error("PDF has no extractable text","PDF 没有可提取文字，可能是扫描图片或空白文件；当前未启用 OCR。");
    return text;
}
