#include "app.hpp"

#include <atomic>
#include <csignal>

namespace {
std::atomic<bool> interrupted{false};
void signalHandler(int) { interrupted = true; }
} // namespace

int main() {
  std::signal(SIGINT, signalHandler);
  rov::App app(interrupted);
  return app.run();
}
