#include "gpu.hpp"
#include "render_file.hpp"
#include "render_proof.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apo;
namespace fs = std::filesystem;
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
template<class Exception, class Action> void rejects(Action action) {
    try { action(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("Expected rejection did not occur");
}

struct Scratch {
    fs::path path = fs::temp_directory_path() /
        ("apo-proof-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { require(fs::create_directory(path), "Cannot create test directory"); }
    ~Scratch() { std::error_code error; fs::remove_all(path, error); }
};

// These fixtures fit in one uncompressed TIFF strip. Read its advertised
// offset and sample depth to compare actual file samples, including alpha.
std::vector<std::uint8_t> export_pixels(const Flame& f, const RenderOptions& options, bool alpha,
                                        bool sixteen_bit, const fs::path& folder) {
    const auto path = folder / "reference.tif";
    FileRenderOptions file;
    file.image = {sixteen_bit ? ImageFormat::tiff16 : ImageFormat::tiff8, alpha};
    file.strip_rows = 6;
    render_to_file(f, options, path, file);
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(in), {});
    auto le = [&](std::size_t at, unsigned size) {
        std::uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i) result |= std::uint32_t(data.at(at+i)) << (8*i);
        return result;
    };
    require(le(0, 4) == 0x002a4949, "Invalid TIFF header");
    const auto directory = le(4, 4);
    auto tag = [&](unsigned number) {
        for (unsigned i = 0; i < le(directory, 2); ++i) {
            const auto at = directory + 2 + i * 12;
            if (le(at, 2) == number) return le(at + 8, 4);
        }
        throw std::runtime_error("Missing TIFF field");
    };
    const unsigned depth = le(tag(258), 2), channels = tag(277);
    require(tag(256) == unsigned(f.width) && tag(257) == unsigned(f.height) &&
            tag(278) >= unsigned(f.height) && depth == (sixteen_bit ? 16u : 8u) &&
            channels == (alpha ? 4u : 3u), "Unexpected TIFF profile");
    auto at = tag(273);
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < f.width*f.height; ++i) {
        for (unsigned c = 0; c < channels; ++c) {
            const auto channel = le(at, depth / 8);
            rgba.push_back(std::uint8_t(depth == 8 ? channel : (channel + 128) / 257));
            at += depth / 8;
        }
        if (!alpha) rgba.push_back(255);
    }
    return rgba;
}
void compare(const std::vector<std::uint8_t>& full, int width, const RenderProof& proof,
             const std::vector<std::uint8_t>& crop) {
    const auto r = proof.region;
    require(crop.size() == std::size_t(r.width)*r.height*4, "Unexpected proof byte count");
    for (int y=0; y<r.height; ++y) {
        const auto from = full.begin() + 4*(std::size_t(y + r.y)*width + r.x);
        const auto to = crop.begin() + 4*std::size_t(y)*r.width;
        require(std::equal(to, to + 4*r.width, from), "Crop differs from final export pixels");
    }
}
Flame fixture() {
    auto f = demo_flame();
    f.width = 37; f.height = 29;
    f.center = {.13, -.12};
    f.rotate = 19.5; f.zoom = .125; f.scale = 9.75;
    f.oversample = 3; f.filter_radius = .7;
    f.gamma = 1.7; f.gamma_threshold = .04; f.vibrancy = .65;
    f.brightness = .55; f.background = {.1,.3,.09};
    f.curves.channels[1][1] = {.17,.31,1};
    return f;
}
void profiles(const fs::path& dir) {
    RenderOptions opts;
    opts.samples = 68659; // Partial last batch and more than one proof chunk.
    opts.threads = 2; opts.memory_mib = 32;
    const std::array<ProofRegion, 4> regions{{{8,5,16,15},{0,0,8,9},{29,22,8,7},{13,9,1,1}}};
    for (int profile = 0; profile < 4; ++profile) {
        auto f = fixture();
        if (profile == 0) { f.filter_radius.reset(); f.oversample = 1; }
        if (profile == 1) f.source_version = "Apophysis 7X";
        if (profile == 2) { f.oversample = 4; f.density_estimator = DensityEstimator{2.5,.5,.7}; }
        if (profile == 3) { f.filter_radius = 0; f.density_estimator = DensityEstimator{1,1,.7}; }
        const bool alpha = profile != 1;
        const bool sixteen = profile == 1 || profile == 2;
        const auto full = export_pixels(f, opts, alpha, sixteen, dir);
        bool has_flame = false;
        for (std::size_t i = 4; i < full.size(); i += 4)
            has_flame |= !std::equal(full.begin(), full.begin() + 4, full.begin() + i);
        require(has_flame, "Reference fixture rendered only its background");
        std::shared_ptr<const RenderProof> center;
        for (const auto rect : regions) {
            const auto proof = sample_render_proof(f, opts, rect);
            require(proof->samples == opts.samples && proof->backend == RenderBackend::cpu &&
                    proof->histogram_bytes == std::uint64_t(rect.width)*rect.height*4*sizeof(double),
                    "Wrong proof identity or retained footprint");
            compare(full, f.width, *proof, develop_render_proof(*proof, proof_tone(f), alpha, {}, sixteen));
            if (!center) center = proof;
        }
        // Develop the original retained histogram again after every compatible
        // tone adjustment, with a different output depth and alpha mode.
        f.brightness = 1.7; f.gamma = 2.6; f.vibrancy = .25; f.gamma_threshold = .12; f.background = {.7,.03,.3};
        compare(export_pixels(f, opts, !alpha, !sixteen, dir), f.width, *center,
                develop_render_proof(*center, proof_tone(f), !alpha, {}, !sixteen));
    }
}
void admission_and_cancel() {
    auto f = fixture();
    RenderOptions opts; opts.samples = 100; opts.memory_mib = 2;
    for (auto bad : {ProofRegion{-1,0,1,1}, ProofRegion{0,0,0,1}, ProofRegion{f.width,0,1,1},
                     ProofRegion{0,0,std::numeric_limits<int>::max(),1}})
        rejects<std::invalid_argument>([&] { sample_render_proof(f, opts, bad); });
    f.width = f.height = 16384;
    rejects<std::invalid_argument>([&] { sample_render_proof(f, opts, {0,0,f.width,f.height}); });
    const auto tiny = sample_render_proof(f, opts, {8170,8190,14,12});
    require(tiny->histogram_bytes == 14*12*32, "Large canvas allocated a full-frame histogram");
    std::stop_source stop;
    opts.stop_token = stop.get_token();
    opts.samples = 100000;
    opts.progress = [&](auto completed, auto) { if (completed) stop.request_stop(); };
    rejects<RenderCancelled>([&] { sample_render_proof(f, opts, {0,0,1,1}); });
    rejects<RenderCancelled>([&] { develop_render_proof(*tiny, proof_tone(f), true, opts.stop_token); });
    auto tone = proof_tone(f);
    tone.gamma = std::numeric_limits<double>::quiet_NaN();
    rejects<std::invalid_argument>([&] { develop_render_proof(*tiny, tone, false); });
}
void backends(const fs::path& dir) {
    const auto f = fixture();
    const ProofRegion crop{5,4,17,19};
    RenderOptions opts; opts.samples = 1037; opts.backend = RenderBackend::vulkan;
    opts.device = "invalid-test-device";
    opts.allow_cpu_fallback = false;
    rejects<std::exception>([&] { sample_render_proof(f, opts, crop); });
    opts.allow_cpu_fallback = true;
    const auto fallback = sample_render_proof(f, opts, crop);
    require(fallback->backend == RenderBackend::cpu && !fallback->fallback_reason.empty(), "CPU fallback was not identified");
    opts.backend = RenderBackend::cpu;
    compare(export_pixels(f, opts, true, false, dir), f.width, *fallback,
            develop_render_proof(*fallback, proof_tone(f), true));
    const auto info = compute_devices();
    const auto dev = std::find_if(info.devices.begin(), info.devices.end(), [](const auto& d) { return d.usable; });
    if (dev != info.devices.end()) {
        opts.device = dev->id; opts.backend = RenderBackend::vulkan; opts.allow_cpu_fallback = false;
        const auto gpu = sample_render_proof(f, opts, crop);
        require(gpu->backend == RenderBackend::vulkan, "GPU proof unexpectedly used CPU");
        compare(export_pixels(f, opts, true, false, dir), f.width, *gpu,
                develop_render_proof(*gpu, proof_tone(f), true));
        std::cout << "Vulkan proof matched on " << dev->name << '\n';
    } else {
        std::cout << "No usable Vulkan device. CPU fallback and strict failure checked.\n";
    }
}
}
int main() {
    try {
        Scratch dir;
        profiles(dir.path);
        admission_and_cancel();
        backends(dir.path);
        std::cout << "Export crop, retained tone, alpha, memory, cancellation and backend checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
