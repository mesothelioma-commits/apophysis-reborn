#include "gpu.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace {
using namespace apo;

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

std::uint64_t digest(const Image &image) {
    std::uint64_t value = UINT64_C(14695981039346656037);
    for (auto byte : image.rgb)
        value = (value ^ byte) * UINT64_C(1099511628211);
    return value;
}

Flame cpu_continuity() {
    Flame flame;
    flame.name = "CPU continuity";
    flame.width = 96;
    flame.height = 72;
    flame.scale = 24;
    flame.brightness = .8;
    const std::array<RGB, 4> colors{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}}};
    for (std::size_t i = 0; i < flame.palette.size(); ++i)
        flame.palette[i] = colors[i / 64];
    const std::array<Point, 4> offsets{{{-.5, -.5}, {.5, -.5}, {-.5, .5}, {.5, .5}}};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        Transform transform;
        transform.pre = {.5, 0, 0, .5, offsets[i].x, offsets[i].y};
        transform.color = double(i) * .25;
        flame.transforms.push_back(transform);
    }
    return flame;
}

Flame impulse(bool historical) {
    Flame flame;
    flame.name = historical ? "Historical impulse" : "Generic final impulse";
    flame.width = 15;
    flame.height = 11;
    flame.scale = 4;
    flame.gamma = 1;
    flame.brightness = .125;
    flame.palette.fill({1, .5, .25});
    Transform transform;
    transform.pre = {.5, 0, 0, .5, 0, 0};
    flame.transforms.push_back(transform);
    flame.final_transform.emplace();
    flame.final_transform->post.e = .5;
    // The centered, odd-sized image keeps the converged orbit away from a pixel
    // edge. Feedback of the final transform would move the lit pixel by two.
    if (historical) {
        flame.source_version = "Apophysis 2.09";
        flame.oversample = 2;
        flame.filter_radius = .5;
        flame.transforms[0].parameters.apophysis_math = true;
        flame.final_transform->parameters.apophysis_math = true;
    }
    return flame;
}

Flame nonlinear(std::string source = {}) {
    Flame flame;
    flame.name = source.empty() ? "Generic nonlinear" : source + " nonlinear";
    flame.source_version = std::move(source);
    flame.width = 128;
    flame.height = 96;
    flame.scale = 24;
    flame.rotate = 13.5;
    flame.gamma = 2.2;
    flame.brightness = .8;
    flame.vibrancy = .75;
    flame.gamma_threshold = .03125;
    flame.background = {.015625, .03125, .0625};
    flame.oversample = 2;
    flame.filter_radius = .5;
    flame.palette_mode = PaletteMode::linear;
    for (std::size_t i = 0; i < flame.palette.size(); ++i)
        flame.palette[i] = {double(i) / 256, double(255 - i) / 256, i < 128 ? .25 : .75};

    Transform first;
    first.pre = {.45, .1, -.05, .5, -.45, -.2};
    first.variations = {{Variation::linear, .6}, {Variation::sinusoidal, .4}};
    first.color = .125;
    first.opacity = .5;
    first.chaos = {0, 1, 2};
    Transform second;
    second.pre = {.5, -.07, .03, .48, .4, -.25};
    second.variations = {{Variation::linear, .5}, {Variation::bubble, .35}, {Variation::noise, .15}};
    second.color = .5;
    second.weight = 2;
    second.chaos = {2, 0, 2};
    Transform third;
    third.pre = {.4, .04, -.08, .5, .05, .5};
    third.variations = {{Variation::fisheye, .45}, {Variation::linear, .55}};
    third.color = .875;
    third.opacity = .75;
    flame.transforms = {first, second, third};
    flame.final_transform.emplace();
    flame.final_transform->pre = {.85, .02, -.02, .9, .04, .05};
    flame.final_transform->post = {.95, 0, 0, 1.05, .03, -.02};
    flame.final_transform->variations = {{Variation::linear, .8}, {Variation::cylinder, .2}};
    flame.final_transform->opacity = .5;
    flame.final_transform->color = .75;
    flame.final_transform->color_speed = .25;
    if (!flame.source_version.empty()) {
        const bool separate_angle = flame.source_version == "Apophysis 7x";
        for (auto &transform : flame.transforms) {
            transform.parameters.apophysis_math = true;
            transform.parameters.separate_blur_angle = separate_angle;
        }
        flame.final_transform->parameters.apophysis_math = true;
        flame.final_transform->parameters.separate_blur_angle = separate_angle;
        flame.transforms[1].variations.insert(flame.transforms[1].variations.begin(),
                                              {Variation::pre_blur, .05});
    }
    return flame;
}

struct Difference {
    double mean = 0, rms = 0;
    unsigned maximum = 0;
    std::size_t changed = 0;
};

Difference difference(const Image &actual, const Image &reference) {
    require(actual.width == reference.width && actual.height == reference.height &&
                actual.rgb.size() == reference.rgb.size(),
            "CPU/compute image dimensions differ");
    Difference result;
    double squared = 0;
    for (std::size_t i = 0; i < actual.rgb.size(); ++i) {
        const unsigned delta = unsigned(std::abs(int(actual.rgb[i]) - int(reference.rgb[i])));
        result.mean += delta;
        squared += double(delta) * delta;
        result.maximum = std::max(result.maximum, delta);
        result.changed += delta != 0;
    }
    result.mean /= actual.rgb.size();
    result.rms = std::sqrt(squared / actual.rgb.size());
    return result;
}

void compare(std::string_view name, const Flame &flame, const RenderOptions &options, bool exact) {
    const auto reference = render_compute_reference(flame, options);
    const auto actual = render(flame, options);
    require(reference.backend == RenderBackend::cpu && reference.fallback_reason.empty(),
            "The double-precision comparison path did not execute on the CPU");
    require(actual.backend == RenderBackend::vulkan && actual.fallback_reason.empty(),
            "A compute comparison silently used CPU fallback");
    require(actual.plotted == reference.plotted && actual.invalid == reference.invalid,
            "Matched-stream sample counts differ");
    require(actual.plotted > options.samples / 4 && actual.invalid == 0,
            "Comparison fixture did not produce useful finite geometry");
    bool structure = false;
    for (std::size_t i = 3; i < reference.image.rgb.size(); i += 3)
        structure |= !std::equal(reference.image.rgb.begin(), reference.image.rgb.begin() + 3,
                                 reference.image.rgb.begin() + i);
    require(structure, "Comparison fixture produced a spatially uniform image");
    const auto delta = difference(actual.image, reference.image);
    std::cout << std::fixed << std::setprecision(6) << "comparison=" << name
              << " mean_byte_error=" << delta.mean << " rms_byte_error=" << delta.rms
              << " max_byte_error=" << delta.maximum << " changed_channels=" << delta.changed
              << " plotted=" << actual.plotted << " invalid=" << actual.invalid << '\n';
    if (exact) {
        require(delta.maximum == 0, "Representable impulse differs from the double reference");
    } else {
        // These bounded, contractive fixtures keep matching random choices and
        // contain no singular orbit. FP32 can move rare histogram hits across a
        // bin boundary. The release gate permits <1/4 of one RGB8 code on average,
        // at most 2 codes RMS, and no channel discrepancy larger than 32/255.
        require(delta.mean <= .25 && delta.rms <= 2 && delta.maximum <= 32,
                "Nonlinear image comparison exceeds the documented RGB8 error limits");
    }
}

void cpu_and_policy_checks(const ComputeInfo &info) {
    const auto flame = cpu_continuity();
    RenderOptions options{65539, 123456, 1};
    const auto original = render(flame, options);
    // Captured from the retained, unmodified v0.5.0 native executable, using this
    // dyadic affine fixture, seed 123456, 65539 samples, and RGB bytes only.
    require(digest(original.image) == UINT64_C(0x09da8711936d84d8),
            "The established CPU renderer changed its pre-M6 golden image");
    require(original.backend == RenderBackend::cpu && original.fallback_reason.empty(),
            "Default rendering must retain the CPU compatibility path");
    options.threads = 3;
    options.backend = RenderBackend::cpu;
    options.device = "__apophysis_missing_device__";
    require(render(flame, options).image.rgb == original.image.rgb,
            "Explicit CPU rendering changed a golden or consulted a GPU selector");
    options.backend = RenderBackend::vulkan;
    options.allow_cpu_fallback = false;
    rejects<gpu::Unavailable>([&] { render(flame, options); },
                              "Strict missing-device selection succeeded");
    options.allow_cpu_fallback = true;
    const auto fallback = render(flame, options);
    require(fallback.backend == RenderBackend::cpu && !fallback.fallback_reason.empty() &&
                fallback.image.rgb == original.image.rgb,
            "Missing-device CPU fallback is not explicit or faithful");

    const bool hardware =
        std::any_of(info.devices.begin(), info.devices.end(),
                    [](const ComputeDevice &device) { return device.usable && !device.software; });
    if (!hardware) {
        options.backend = RenderBackend::automatic;
        options.device.clear();
        const auto automatic = render(flame, options);
        require(automatic.backend == RenderBackend::cpu && !automatic.fallback_reason.empty() &&
                    automatic.image.rgb == original.image.rgb,
                "Automatic selection used a software Vulkan device or concealed its CPU fallback");
    }
    options.backend = RenderBackend::vulkan;
    options.samples = 0;
    rejects<std::invalid_argument>([&] { render(flame, options); },
                                   "Invalid samples fell back instead of failing");
    options.samples = 65539;
    options.memory_mib = 1;
    auto huge = flame;
    huge.width = huge.height = 1024;
    rejects<std::invalid_argument>([&] { render(huge, options); },
                                   "Host budget violation fell back instead of failing");
    options.memory_mib = 512;
    auto invalid = flame;
    invalid.gamma = 0;
    rejects<std::invalid_argument>([&] { render(invalid, options); },
                                   "Invalid model fell back instead of failing");
    std::cout << "CPU golden and backend policy checks passed\n";
}

void device_checks(const ComputeDevice &device) {
    RenderOptions options{65539, UINT64_C(0x9f123456abcdef01), 2};
    options.backend = RenderBackend::vulkan;
    options.device = device.id;
    options.allow_cpu_fallback = false;
    std::cout << "device=" << device.name << " type=" << device.type << " software=" << device.software
              << " api=" << device.api_version << '\n';
    compare("generic-impulse", impulse(false), options, true);
    compare("classic-filtered-impulse", impulse(true), options, true);
    compare("generic-nonlinear-chaos-final", nonlinear(), options, false);
    compare("classic-preblur", nonlinear("Apophysis 2.09"), options, false);
    compare("seven-preblur", nonlinear("Apophysis 7x"), options, false);

    const auto flame = nonlinear();
    auto first = render(flame, options);
    auto repeated = render(flame, options);
    require(first.image.rgb == repeated.image.rgb && first.plotted == repeated.plotted &&
                first.invalid == repeated.invalid,
            "Repeated GPU dispatch changed a seeded result");
    ++options.seed;
    require(render(flame, options).image.rgb != first.image.rgb,
            "GPU seed does not affect stochastic output");
    options.backend = RenderBackend::automatic;
    require(render(flame, options).backend == RenderBackend::vulkan,
            "An explicitly selected usable device was ignored in automatic mode");
    options.backend = RenderBackend::vulkan;

    std::uint64_t last = 0;
    unsigned callbacks = 0;
    options.progress = [&](std::uint64_t completed, std::uint64_t total) {
        require(total == options.samples && completed >= last && completed <= total,
                "GPU progress is not monotonic committed-sample progress");
        last = completed;
        ++callbacks;
    };
    render(impulse(false), options);
    require(last == options.samples && callbacks >= 2,
            "GPU progress did not cover start and completion");
    std::stop_source stop;
    options.stop_token = stop.get_token();
    stop.request_stop();
    rejects<RenderCancelled>([&] { render(flame, options); }, "Pre-cancelled GPU render proceeded");
    stop = std::stop_source();
    options.stop_token = stop.get_token();
    options.samples = 524291; // More than two 1024-chain dispatches, including a partial final chain.
    options.progress = [&](std::uint64_t completed, std::uint64_t) {
        if (completed)
            stop.request_stop();
    };
    options.allow_cpu_fallback = true;
    rejects<RenderCancelled>([&] { render(flame, options); },
                             "Cancelled GPU render continued or fell back");
    options.stop_token = {};
    options.progress = {};
    options.samples = 65539;
    options.allow_cpu_fallback = false;

    auto oversized = impulse(false);
    oversized.width = oversized.height = 256;
    options.gpu_memory_mib = 1;
    rejects<gpu::Unavailable>([&] { render(oversized, options); },
                              "Strict GPU memory budget was ignored");
    options.allow_cpu_fallback = true;
    auto fallback = render(oversized, options);
    require(fallback.backend == RenderBackend::cpu && !fallback.fallback_reason.empty(),
            "GPU budget fallback did not report the actual CPU backend");
    options.gpu_memory_mib = 512;

    auto unrepresentable = impulse(false);
    unrepresentable.transforms[0].pre.a = 1e-50;
    require(!compute_support_reason(unrepresentable).empty(), "FP32 underflow was silently admitted");
    fallback = render(unrepresentable, options);
    require(fallback.backend == RenderBackend::cpu && !fallback.fallback_reason.empty(),
            "Unsupported FP32 model did not report CPU fallback");
    options.allow_cpu_fallback = false;
    rejects<gpu::Unavailable>([&] { render(unrepresentable, options); },
                              "Strict FP32 admission guard was ignored");
    std::cout << "Repeatability, explicit selection, progress, cancellation and limit checks passed\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::string selected;
        bool require_vulkan = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--require-vulkan") {
                require_vulkan = true;
            } else if (arg == "--device" && i + 1 < argc && selected.empty()) {
                selected = argv[++i];
                require(!selected.empty(), "--device needs a discovery ID or decimal index");
            } else {
                throw std::invalid_argument("Usage: gpu-tests [--require-vulkan] [--device ID|INDEX]");
            }
        }
        const auto info = apo::compute_devices();
        cpu_and_policy_checks(info);
        const auto device = std::find_if(
            info.devices.begin(), info.devices.end(), [&](const apo::ComputeDevice &candidate) {
                return candidate.usable && (selected.empty() || selected == candidate.id ||
                                            selected == std::to_string(candidate.index));
            });
        if (device == info.devices.end()) {
            if (require_vulkan || !selected.empty())
                throw std::runtime_error("No usable requested Vulkan device; " + info.diagnostic);
            std::cout << "SKIP: no usable Vulkan device; " << info.diagnostic << '\n';
            return 77;
        }
        device_checks(*device);
        std::cout << "GPU integration checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "GPU integration test failed: " << error.what() << '\n';
        return 1;
    }
}
