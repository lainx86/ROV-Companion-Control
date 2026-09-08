#pragma once

#include "config.hpp"
#include "process.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rov {

class Controller {
public:
  Controller(ManagedProcess::Log log, std::function<void()> changed);
  void start(const Config &cfg);
  void stop();
  std::map<std::string, bool> states();

private:
  ManagedProcess::Log log_;
  std::function<void()> changed_;
  std::vector<std::unique_ptr<ManagedProcess>> processes_;
};

} // namespace rov
