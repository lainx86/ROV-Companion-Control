#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace rov {

// Physical wired Ethernet only; never falls back to the default/Wi-Fi route.
std::pair<std::string, std::string> localIpv4();
bool isEthernetInterface(const std::string &name,
                         const std::string &sysfs_root = "/sys/class/net");
std::vector<std::string> cidrHosts(const std::string &cidr);

// Prefer the saved target; an ambiguous scan requires manual selection.
std::string chooseScanTarget(const std::vector<std::string> &ips,
                             const std::string &preferred_ip);

struct ScanResult {
  std::vector<std::string> ips;
  std::string error;
};

struct TargetProbeResult {
  bool reachable{false};
  std::string error;
};

// Probes one target over the active physical Ethernet interface only.
TargetProbeResult probeTargetOnEthernet(const std::string &ip,
                                        const std::atomic<bool> &cancelled);

// Blocking scan; call from a worker thread. Cancellation stops scheduling
// probes.
ScanResult scanNetwork(const std::atomic<bool> &cancelled);

} // namespace rov
