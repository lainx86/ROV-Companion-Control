#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace rov {

std::pair<std::string, std::string> localIpv4();
std::vector<std::string> cidrHosts(const std::string &cidr);

struct ScanResult {
  std::vector<std::string> ips;
  std::string error;
};

// Blocking scan; call from a worker thread. Cancellation stops scheduling
// probes.
ScanResult scanNetwork(const std::atomic<bool> &cancelled);

} // namespace rov
