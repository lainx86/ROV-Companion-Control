#define main rov_control_application_main
#include "../src/main.cpp"
#undef main

#include <cassert>

int main() {
  Config config;
  std::string error;
  assert(validate(config, error));

  config.cam0_port = 0;
  assert(!validate(config, error));
  config.cam0_port = 5070;

  const auto hosts = cidrHosts("192.168.1.0/30");
  assert((hosts == std::vector<std::string>{"192.168.1.1", "192.168.1.2"}));
  assert(cidrHosts("192.168.0.0/15").empty());
  assert(routineOutput("Setting pipeline to PLAYING ..."));
  assert(!routineOutput("ERROR: camera unavailable"));
  return 0;
}
