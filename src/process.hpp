#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace rov {

bool routineOutput(const std::string &line);

class ManagedProcess {
public:
  using Log = std::function<void(const std::string &)>;
  ManagedProcess(std::string name, std::vector<std::string> command, Log log,
                 std::function<void()> changed);
  ~ManagedProcess();
  bool start();
  bool running();
  void stop();
  const std::string &name() const;

private:
  void readOutput(int fd);
  std::string name_;
  std::vector<std::string> command_;
  Log log_;
  std::function<void()> changed_;
  pid_t pid_{-1};
  std::mutex lock_;
  std::thread reader_;
};

} // namespace rov
