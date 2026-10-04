#include "app.hpp"

#include <atomic>
#include <clocale>
#include <csignal>

namespace {
std::atomic<bool> interrupted{false};
void signalHandler(int) { interrupted = true; }
} // namespace

int main() {
  std::setlocale(LC_ALL, "");
  std::signal(SIGINT, signalHandler);
  rov::App app(interrupted);
  return app.run();
}
