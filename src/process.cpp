#include "process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace rov {

bool routineOutput(const std::string &line) {
  static const std::vector<std::string> ignored{
      "Setting pipeline to ", "Pipeline is PREROLL",
      "New clock: ", "Redistribute latency"};
  return std::any_of(ignored.begin(), ignored.end(), [&](const auto &prefix) {
    return line.rfind(prefix, 0) == 0;
  });
}

ManagedProcess::ManagedProcess(std::string name,
                               std::vector<std::string> command, Log log,
                               std::function<void()> changed)
    : name_(std::move(name)), command_(std::move(command)),
      log_(std::move(log)), changed_(std::move(changed)) {}

ManagedProcess::~ManagedProcess() { stop(); }

bool ManagedProcess::start() {
  if (running()) {
    log_("[INFO] " + name_ + " sudah berjalan");
    return true;
  }
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    log_("[ERROR] Pipe " + name_ + ": " + std::strerror(errno));
    return false;
  }
  const pid_t child = fork();
  if (child == 0) {
    setsid();
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    std::vector<char *> argv;
    for (auto &part : command_)
      argv.push_back(const_cast<char *>(part.c_str()));
    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    dprintf(STDERR_FILENO, "exec %s gagal: %s\n", argv.front(),
            std::strerror(errno));
    _exit(127);
  }
  if (child < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    log_("[ERROR] Fork " + name_ + ": " + std::strerror(errno));
    return false;
  }
  close(pipefd[1]);
  {
    std::lock_guard<std::mutex> guard(lock_);
    pid_ = child;
  }
  reader_ = std::thread([this, fd = pipefd[0]] { readOutput(fd); });
  log_("[INFO] Starting " + name_ + "...");
  changed_();
  return true;
}

bool ManagedProcess::running() {
  std::lock_guard<std::mutex> guard(lock_);
  if (pid_ <= 0)
    return false;
  int status = 0;
  const pid_t result = waitpid(pid_, &status, WNOHANG);
  if (result == pid_) {
    pid_ = -1;
    changed_();
    return false;
  }
  return result == 0;
}

void ManagedProcess::stop() {
  pid_t pid = -1;
  {
    std::lock_guard<std::mutex> guard(lock_);
    pid = pid_;
  }
  if (pid <= 0) {
    if (reader_.joinable())
      reader_.join();
    return;
  }
  log_("[INFO] Stopping " + name_ + "...");
  kill(-pid, SIGTERM);
  int status = 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline &&
         waitpid(pid, &status, WNOHANG) == 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  if (waitpid(pid, &status, WNOHANG) == 0) {
    log_("[WARNING] " + name_ + " force kill");
    kill(-pid, SIGKILL);
    waitpid(pid, &status, 0);
  }
  {
    std::lock_guard<std::mutex> guard(lock_);
    pid_ = -1;
  }
  if (reader_.joinable())
    reader_.join();
  changed_();
}

const std::string &ManagedProcess::name() const { return name_; }

void ManagedProcess::readOutput(int fd) {
  FILE *stream = fdopen(fd, "r");
  if (!stream) {
    close(fd);
    return;
  }
  char *line = nullptr;
  size_t length = 0;
  while (getline(&line, &length, stream) != -1) {
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
      text.pop_back();
    if (!text.empty() && !routineOutput(text))
      log_("[" + name_ + "] " + text);
  }
  free(line);
  fclose(stream);
}

} // namespace rov
