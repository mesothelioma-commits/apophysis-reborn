#include "gpu.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <stop_token>
#include <string_view>

namespace {
using namespace apo;
using Hit = std::array<std::uint32_t, 6>;

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}
template <class Exception, class Function> void rejects(Function &&function, std::string_view message) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

gpu::Geometry geometry(const Flame &flame) {
    unsigned padding = 0;
    if (flame.filter_radius) {
        double diameter = 2 * (is_apophysis(flame.source_version) ? 1.8 : 1.5) * flame.oversample *
                          *flame.filter_radius;
        if (is_apophysis(flame.source_version))
            diameter = std::floor(diameter);
        unsigned taps = static_cast<unsigned>(diameter) + 1;
        taps += (taps ^ unsigned(flame.oversample)) & 1u;
        if (taps > unsigned(flame.oversample))
            padding = (taps - unsigned(flame.oversample)) / 2;
    }
    const auto angle = std::remainder(flame.rotate, 360.0) * std::numbers::pi / 180.0;
    return {std::uint32_t(flame.width * flame.oversample + 2 * padding),
            std::uint32_t(flame.height * flame.oversample + 2 * padding),
            padding,
            flame.scale * std::exp2(flame.zoom),
            std::cos(angle),
            std::sin(angle)};
}

Flame fixture(std::string source = {}) {
    auto flame = demo_flame();
    flame.source_version = std::move(source);
    flame.width = 96;
    flame.height = 72;
    flame.scale = 24;
    flame.rotate = 13.5;
    flame.oversample = 2;
    flame.filter_radius = .5;
    flame.palette_mode = PaletteMode::linear;
    for (std::size_t i = 0; i < flame.transforms.size(); ++i) {
        auto &transform = flame.transforms[i];
        transform.opacity = .5 + double(i) * .125;
        transform.chaos = {1, 2, 0};
        transform.parameters.apophysis_math = !flame.source_version.empty();
        transform.parameters.separate_blur_angle = flame.source_version == "Apophysis 7x";
    }
    flame.transforms[1].variations = {{Variation::pre_blur, .05},
                                      {Variation::linear, .6},
                                      {Variation::noise, .05},
                                      {Variation::bubble, .35}};
    flame.final_transform.emplace();
    flame.final_transform->pre = {.9, .02, -.01, .9, .03, -.04};
    flame.final_transform->opacity = .5;
    flame.final_transform->parameters.apophysis_math = !flame.source_version.empty();
    flame.final_transform->variations = {{Variation::linear, .8}, {Variation::cylinder, .2}};
    validate(flame);
    return flame;
}

void basic_contract() {
    const auto flame = fixture();
    const auto geo = geometry(flame);
    RenderOptions options;
    options.samples = 513;
    for (auto count : {0u, 1025u})
        rejects<std::invalid_argument>([&] { gpu::sample_hits(flame, options, geo, 0, count, 0); },
                                       "Invalid hit count was accepted");
    rejects<std::invalid_argument>([&] { gpu::sample_hits(flame, options, geo, 3, 1, 0); },
                                   "A chain beyond the whole job was accepted");
    rejects<std::invalid_argument>([&] { gpu::sample_hits(flame, options, geo, 2, 2, 0); },
                                   "A chain range extending beyond the job was accepted");
    rejects<std::invalid_argument>([&] { gpu::sample_hits(flame, options, geo, UINT64_MAX, 1, 0); },
                                   "An overflowing chain range was accepted");
    options.samples = 0;
    rejects<std::invalid_argument>([&] { gpu::sample_hits(flame, options, geo, 0, 1, 0); },
                                   "Zero samples were accepted");
    options.samples = UINT64_MAX;
    rejects<std::invalid_argument>([&] { gpu::sample_hits(flame, options, geo, 0, 1, 0); },
                                   "Unbounded sample count was accepted");
    std::stop_source cancel;
    cancel.request_stop();
    options.stop_token = cancel.get_token();
    rejects<RenderCancelled>([&] { gpu::sample_hits(flame, options, geo, 0, 1, 0); },
                             "Pre-cancelled hit request was not cancelled");

    auto large = fixture();
    large.width = large.height = 16384;
    large.scale = 4096;
    const auto large_geo = geometry(large);
    rejects<std::invalid_argument>([&] { gpu::pack_flame(large, large_geo); },
                                   "Dense packing unexpectedly admitted a word-address overflow");
    require(!gpu::pack_flame(large, large_geo, true).words.empty(),
            "Hit packing could not admit separate global coordinates");
    auto excessive = large_geo;
    excessive.width = 16777217;
    rejects<std::invalid_argument>([&] { gpu::pack_flame(large, excessive, true); },
                                   "Hit packing waived the exact FP32 coordinate limit");
}

void compare(const Flame &flame, const RenderOptions &options) {
    const auto geo = geometry(flame);
    const auto count = std::uint32_t(1 + (options.samples - 1) / gpu::samples_per_chain);
    const auto whole = gpu::sample_hits(flame, options, geo, 0, count, 0);
    const auto dense = gpu::sample(flame, options, geo, std::uint64_t(geo.width) * geo.height * 32);
    std::vector<std::array<std::uint64_t, 4>> folded(std::size_t(geo.width) * geo.height);
    for (const auto &hit : whole.hits) {
        auto &bin = folded[std::size_t(hit[1]) * geo.width + hit[0]];
        for (unsigned channel = 0; channel < 4; ++channel)
            bin[channel] += hit[channel + 2];
    }
    require(folded == dense.bins && whole.plotted == dense.plotted && whole.invalid == dense.invalid,
            "Streamed hits differ from dense Vulkan histogram/counters");
    require(!whole.device_name.empty() && !whole.checkpoint_identity.empty(),
            "Streamed hit result omits its execution identity");
    require(whole.checkpoint_identity.find("spirv-fnv1a64=") != std::string::npos,
            "Checkpoint identity omits the compiled shader fingerprint");
    std::vector<Hit> split;
    std::uint64_t invalid = 0;
    for (std::uint32_t base = 0; base < count;) {
        const auto n = std::min(count - base, base ? 37u : 1u);
        auto part = gpu::sample_hits(flame, options, geo, base, n, 0);
        require(part.checkpoint_identity == whole.checkpoint_identity,
                "Device identity changed between the same job's chunks");
        split.insert(split.end(), part.hits.begin(), part.hits.end());
        invalid += part.invalid;
        base += n;
    }
    require(split == whole.hits && invalid == whole.invalid,
            "Changing chunk boundaries changed the global hit stream");
    std::cout << "stream=" << (flame.source_version.empty() ? "generic" : flame.source_version)
              << " plotted=" << whole.plotted << " invalid=" << whole.invalid
              << " dense_bins_exact=yes split_records_exact=yes\n";
}

void large_and_limits(RenderOptions options) {
    auto flame = fixture();
    flame.width = flame.height = 16384;
    flame.scale = 4096;
    const auto geo = geometry(flame);
    options.samples = 16387;
    options.memory_mib = 16;
    options.gpu_memory_mib = 8;
    unsigned callbacks = 0;
    options.progress = [&](auto, auto) { ++callbacks; };
    const auto large = gpu::sample_hits(flame, options, geo, 0, 65, 1024 * 1024);
    require(large.plotted == large.hits.size() && large.plotted > 1000 && large.invalid == 0,
            "Large global coordinate stream did not produce useful valid samples");
    require(callbacks == 0, "Hit sampler reported progress before an outer chunk commit");
    require(gpu::sample_hits(flame, options, geo, 0, 65, 1024 * 1024).hits == large.hits,
            "Repeated bounded stream changed records");
    rejects<gpu::Unavailable>([&] { gpu::sample(flame, options, geo, 0); },
                              "Dense Vulkan unexpectedly accepted a huge addressed buffer");
    rejects<gpu::Unavailable>(
        [&] { gpu::sample_hits(flame, options, geo, 0, 65, options.memory_mib * 1024 * 1024); },
        "Caller-reserved host storage was not charged");
    options.gpu_memory_mib = 1;
    options.samples = 262144;
    rejects<gpu::Unavailable>([&] { gpu::sample_hits(flame, options, geo, 0, 1024, 0); },
                              "Insufficient device hit storage was not rejected");

    options.gpu_memory_mib = 8;
    options.samples = (std::uint64_t(UINT32_MAX) + 2) * gpu::samples_per_chain - 13;
    const auto across = gpu::sample_hits(flame, options, geo, UINT32_MAX, 2, 0);
    auto first = gpu::sample_hits(flame, options, geo, UINT32_MAX, 1, 0);
    const auto last = gpu::sample_hits(flame, options, geo, std::uint64_t(UINT32_MAX) + 1, 1, 0);
    first.hits.insert(first.hits.end(), last.hits.begin(), last.hits.end());
    require(first.hits == across.hits && first.invalid + last.invalid == across.invalid,
            "64-bit chain-index carry or the high-index partial chain changed "
            "sampling");
    std::cout << "16384x16384, oversample=2, host_budget=16MiB, GPU_budget=8MiB: "
                 "bounded global hit stream passed; 64-bit chain-index carry passed\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool required = false;
        std::string selector;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--require-vulkan")
                required = true;
            else if (std::string_view(argv[i]) == "--device" && i + 1 < argc)
                selector = argv[++i];
            else
                throw std::invalid_argument(
                    "Use gpu-stream-tests [--require-vulkan] [--device ID|INDEX]");
        }
        basic_contract();
        const auto info = compute_devices();
        const auto device = std::find_if(info.devices.begin(), info.devices.end(), [&](const auto &d) {
            return d.usable &&
                   (selector.empty() || selector == d.id || selector == std::to_string(d.index));
        });
        if (device == info.devices.end()) {
            std::cout << "No requested usable Vulkan device; no hit-stream execution "
                         "performed.\n";
            return required || !selector.empty() ? 1 : 77;
        }
        apo::gpu::ContextScope contextScope;
        RenderOptions options;
        options.samples = 65539;
        options.seed = UINT64_C(0x9f123456abcdef01);
        options.backend = RenderBackend::vulkan;
        options.device = device->id;
        options.allow_cpu_fallback = false;
        std::cout << "Hit stream device: " << device->name
                  << (device->software ? " [software Vulkan]" : " [hardware Vulkan]") << '\n';
        for (const auto &source : {"", "Apophysis 2.09", "Apophysis 7x"})
            compare(fixture(source), options);
        auto invalid = fixture();
        invalid.transforms.resize(1);
        invalid.transforms[0].chaos.clear();
        invalid.transforms[0].pre = {1e20, 0, 0, 1e20, 0, 0};
        compare(invalid, options);
        large_and_limits(options);
        std::cout << "Vulkan hit streaming checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "GPU hit-stream test failed: " << error.what() << '\n';
        return 1;
    }
}
