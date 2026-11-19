#pragma once
#include "render.hpp"

namespace apo {
struct ProofRegion { int x, y, width, height; };
struct ProofTone {
    double brightness, gamma, vibrancy, gamma_threshold;
    RGB background;
};
ProofTone proof_tone(const Flame&);

struct ProofHistogram;
class RenderProof {
public:
    ProofRegion region{};
    std::uint64_t samples = 0, histogram_bytes = 0;
    RenderBackend backend = RenderBackend::cpu;
    std::string device_name = "CPU", fallback_reason;
    RenderProof(const RenderProof&) = delete;
    RenderProof& operator=(const RenderProof&) = delete;
private:
    RenderProof() = default;
    std::shared_ptr<const ProofHistogram> histogram_;
    friend std::shared_ptr<const RenderProof> sample_render_proof(const Flame&, const RenderOptions&, ProofRegion);
    friend std::vector<std::uint8_t> develop_render_proof(const RenderProof&, const ProofTone&, bool, std::stop_token, bool);
};

// Coordinates are final output pixels, measured from the top left. Sampling
// uses the complete export camera, sample count, seed and backend stream.
std::shared_ptr<const RenderProof> sample_render_proof(const Flame&, const RenderOptions&, ProofRegion);
// Packed straight RGBA8, with no checkerboard. sixteen_bit follows 16-bit
// export reconstruction and quantization before conversion to display depth.
std::vector<std::uint8_t> develop_render_proof(const RenderProof&, const ProofTone&, bool alpha,
                                              std::stop_token stop = {}, bool sixteen_bit = false);
}
