#include "controller.hpp"

#include <algorithm>
#include <utility>

namespace rov {

Controller::Controller(ManagedProcess::Log log, std::function<void()> changed)
    : log_(std::move(log)), changed_(std::move(changed)) {}

void Controller::start(const Config &cfg) {
  if (std::any_of(processes_.begin(), processes_.end(),
                  [](const auto &process) { return process->running(); })) {
    log_("[WARNING] Ada proses yang masih berjalan");
    return;
  }
  stop();
  processes_.clear();
  auto camera = [&](const std::string &device, int port) {
    return std::vector<std::string>{
        "gst-launch-1.0",
        "-q",
        "v4l2src",
        "device=" + device,
        "!",
        "image/jpeg,width=1280,height=720,framerate=30/1",
        "!",
        "queue",
        "max-size-buffers=2",
        "leaky=downstream",
        "!",
        "jpegdec",
        "!",
        "videoconvert",
        "!",
        "video/x-raw,format=I420",
        "!",
        "x264enc",
        "tune=zerolatency",
        "bitrate=2000",
        "speed-preset=ultrafast",
        "!",
        "rtph264pay",
        "config-interval=1",
        "pt=96",
        "!",
        "udpsink",
        "host=" + cfg.ip,
        "port=" + std::to_string(port),
        "sync=false",
        "async=false"};
  };
  processes_.push_back(std::make_unique<ManagedProcess>(
      "CAM0", camera(cfg.cam0_device, cfg.cam0_port), log_, changed_));
  processes_.push_back(std::make_unique<ManagedProcess>(
      "CAM1", camera(cfg.cam1_device, cfg.cam1_port), log_, changed_));
  processes_.push_back(std::make_unique<ManagedProcess>(
      "MAVProxy",
      std::vector<std::string>{
          "mavproxy.py", "--master=" + cfg.mav_device,
          "--baudrate=" + std::to_string(cfg.baudrate),
          "--out=udp:" + cfg.ip + ":" + std::to_string(cfg.mav_port0),
          "--out=udp:" + cfg.ip + ":" + std::to_string(cfg.mav_port1)},
      log_, changed_));
  log_("[INFO] Memulai semua stream...");
  for (auto &process : processes_)
    process->start();
  changed_();
}

void Controller::stop() {
  for (auto it = processes_.rbegin(); it != processes_.rend(); ++it)
    (*it)->stop();
  changed_();
}

std::map<std::string, bool> Controller::states() {
  std::map<std::string, bool> out{
      {"CAM0", false}, {"CAM1", false}, {"MAVProxy", false}};
  for (const auto &process : processes_)
    out[process->name()] = process->running();
  return out;
}

} // namespace rov
