#pragma once

#include "../gpu.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace apo::editor {

// Driver discovery can block inside a vendor library and has no safe forced
// cancellation API. Keep at most one probe per service, with no QObject or GUI
// callback in its lifetime. A dialog may disappear while the pure worker state
// remains alive; destroying the service never joins a stalled driver.
class DeviceDiscovery {
public:
  using Probe = std::function<ComputeInfo()>;
  struct Snapshot {
    std::optional<ComputeInfo> report;
    bool pending = false;
    std::uint64_t revision = 0;
  };

  explicit DeviceDiscovery(Probe probe = compute_devices)
      : state_(std::make_shared<State>(std::move(probe))) {}

  Snapshot snapshot() const {
    std::lock_guard lock(state_->mutex);
    return state_->value;
  }

  bool request(bool refresh = false) {
    const auto state = state_;
    {
      std::lock_guard lock(state->mutex);
      if (state->value.pending || (!refresh && state->value.report))
        return false;
      state->value.pending = true;
    }
    try {
      std::thread([state] {
        ComputeInfo report;
        try {
          report = state->probe();
        } catch (const std::exception &error) {
          report.diagnostic = error.what();
        } catch (...) {
          report.diagnostic = "Device discovery failed with an unknown error";
        }
        std::lock_guard lock(state->mutex);
        state->value.report = std::move(report);
        state->value.pending = false;
        ++state->value.revision;
      }).detach();
    } catch (const std::exception &error) {
      std::lock_guard lock(state->mutex);
      ComputeInfo report;
      report.diagnostic = error.what();
      state->value.report = std::move(report);
      state->value.pending = false;
      ++state->value.revision;
    }
    return true;
  }

private:
  struct State {
    explicit State(Probe value) : probe(std::move(value)) {}
    std::mutex mutex;
    Snapshot value;
    const Probe probe;
  };
  std::shared_ptr<State> state_;
};
} // namespace apo::editor
