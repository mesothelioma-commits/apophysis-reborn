#pragma once
#include "render.hpp"

#include <memory>
#include <string>
#include <vector>

namespace apo::gpu {
// Place on the render thread's stack to reuse its device and pipeline across
// calls. Nested scopes share the outer cache. Ordinary unscoped calls own their
// device until the caller returns.
class ContextScope {
  public:
    ContextScope() noexcept;
    ~ContextScope();
    ContextScope(const ContextScope &) = delete;
    ContextScope &operator=(const ContextScope &) = delete;

  private:
    std::shared_ptr<void> context_; // Opaque Vulkan implementation.
};
} // namespace apo::gpu

namespace apo {
struct ComputeDevice {
    unsigned index = 0;
    std::string id, name, type, api_version, driver_version;
    std::uint64_t device_memory_mib = 0, max_storage_buffer_bytes = 0;
    bool usable = false, software = false, float64 = false, int64 = false;
    std::string reason;
};
struct ComputeInfo {
    bool compiled = false, loader_available = false;
    std::string diagnostic;
    std::vector<ComputeDevice> devices;
};
// Discovery never creates a rendering device or changes a flame.
ComputeInfo compute_devices();
// Empty means the model is representable by the FP32 compute kernel. Device
// storage limits and the configured budgets are checked separately at dispatch.
std::string compute_support_reason(const Flame &flame);
// Numerical comparison path: double CPU formulas with the compute kernel's
// portable RNG and chain boundaries. The ordinary CPU renderer is unchanged.
RenderResult render_compute_reference(const Flame &flame, const RenderOptions &options);
} // namespace apo
