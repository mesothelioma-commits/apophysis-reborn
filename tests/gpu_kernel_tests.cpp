#include "gpu_kernel.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace apo;
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F &&action, const std::string &name) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, name + " was accepted");
}
double stored_float(std::uint32_t word) {
    return std::bit_cast<float>(word);
}

void portable_streams() {
    for (auto seed : {UINT64_C(0), UINT64_C(1), UINT64_MAX, UINT64_C(0xabcdef0123456789)}) {
        for (auto stream : {UINT64_C(0), UINT64_C(1), UINT64_C(0x100000000), UINT64_MAX}) {
            auto state = gpu::portable_seed(seed, stream);
            Rng reference(seed, stream);
            for (unsigned i = 0; i < 1024; ++i) {
                const auto actual = gpu::portable_unit(state);
                check(actual >= 0 && actual < 1 && actual == reference.unit(), "Portable RNG mismatch");
                check(actual == double(float(actual)),
                      "Portable draw is not exactly binary32 representable");
            }
        }
    }
    check(gpu::portable_seed(1, 0) != gpu::portable_seed(UINT64_C(0x100000001), 0),
          "Seed high bits ignored");
    check(gpu::portable_seed(1, 0) != gpu::portable_seed(1, UINT64_C(0x100000000)),
          "Stream high bits ignored");
    Rng original(123456), duplicate(123456), portable(123456, 0);
    check(original.unit() == duplicate.unit() && original.unit() != portable.unit(),
          "CPU stream changed");
}

void packing_and_admission() {
    auto f = demo_flame();
    f.width = 64;
    f.height = 48;
    f.transforms[0].variations = {{Variation::pre_blur, .25},
                                  {Variation::julia, 0},
                                  {Variation::linear, -.5},
                                  {Variation::swirl, .75}};
    f.transforms[0].chaos = {0, 1, 2};
    f.transforms[1].chaos = {1, 0, 0};
    f.final_transform = f.transforms[0];
    f.final_transform->chaos.clear();
    const auto packed = gpu::pack_flame(f, {132, 100, 2, 20, .8, .6});
    const auto &words = packed.words;
    check(words[0] == 3 && words[1] == 3 && words[7] == 132 && words[8] == 100 && words[9] == 2,
          "Packed geometry or final slot incorrect");
    check(words[18] == words.size() && words[2] == gpu::header_words, "Packed offsets incorrect");
    const auto transform = words[2];
    check(words[transform + 20] == 2 && stored_float(words[transform + 21]) == .25,
          "Pre-blur or zero stochastic term packing incorrect");
    check(words[words[transform + 19]] == unsigned(Variation::linear), "Variation order changed");
    const TransformChooser chooser(f);
    const auto cdf = words[3];
    for (std::size_t i = 0; i < 3; ++i)
        check(stored_float(words[cdf + i]) == float(chooser.initial()[i]), "Initial CDF mismatch");
    check(stored_float(words[cdf + 3]) == 0 && stored_float(words[cdf + 5]) == 1,
          "Zero chaos destination or final CDF boundary changed");
    check(stored_float(words[cdf + 6]) == 1 && stored_float(words[cdf + 7]) == 1,
          "Trailing zero chaos destinations changed");
    auto too_many = f;
    too_many.transforms.assign(65, Transform{});
    too_many.final_transform.reset();
    check(!gpu::support_reason(too_many).empty(), "65 ordinary transforms admitted");
    for (double value : {1e100, 1e-100}) {
        auto bad = demo_flame();
        bad.transforms[0].pre.a = value;
        check(!gpu::support_reason(bad).empty(), "Out-of-range FP32 affine admitted");
    }
    auto tiny_probability = demo_flame();
    tiny_probability.transforms[2].weight = 1e-10;
    check(!gpu::support_reason(tiny_probability).empty(), "Collapsed positive CDF interval admitted");
    tiny_probability = demo_flame();
    tiny_probability.transforms[0].weight = 1e-10;
    check(!gpu::support_reason(tiny_probability).empty(),
          "Probability below portable RNG resolution admitted");
    for (auto id : {Variation::waves, Variation::rings, Variation::fan}) {
        auto bad = demo_flame();
        bad.transforms[0].variations = {{id, 1}};
        bad.transforms[0].parameters.apophysis_math = true;
        bad.transforms[0].pre.e = 0;
        check(!gpu::support_reason(bad).empty(), "Unrepresentable historical affine guard admitted");
    }
    auto parameterized = demo_flame();
    auto &term = parameterized.transforms[0];
    term.variations = {{Variation::rectangles, 1}};
    term.parameters.rectangles_x = 1e-100;
    check(!gpu::support_reason(parameterized).empty(), "Underflowed active rectangle spacing admitted");
    term.variations = {{Variation::linear, 1}, {Variation::rectangles, 0}};
    check(gpu::support_reason(parameterized).empty(), "Dormant spacing changed GPU eligibility");
    term.variations = {{Variation::pie, 1}};
    term.parameters.pie_slices = 16777217;
    check(!gpu::support_reason(parameterized).empty(), "Unrepresentable pie slice count admitted");
    term.variations = {{Variation::juliascope, 1}};
    term.parameters.juliascope_power = 16777217;
    check(!gpu::support_reason(parameterized).empty(), "Unrepresentable JuliaScope branch count admitted");
    rejects([&] { gpu::pack_flame(f, {UINT32_MAX, 2, 0, 1, 1, 0}); }, "Overflowing histogram");
    rejects([&] { gpu::pack_flame(f, {64, 48, 0, 1e100, 1, 0}); }, "Overflowing projection");
    rejects([&] { gpu::make_probe(0, {}, 0, 0, 0); }, "Zero-repeat probe");
    std::cout << "Packing, FP32 admission, CDF preservation and 16,384 portable RNG draws passed\n";
}

struct ExpectedProbe {
    gpu::ProbeRecord input;
    Orbit output;
    double next_unit;
    std::string description;
};
std::vector<ExpectedProbe> build_probes(const Flame &f) {
    const std::array<Point, 8> inputs{{{.25, .75},
                                       {-.625, .375},
                                       {-.75, -.25},
                                       {.125, -.875},
                                       {0, 1},
                                       {1, 0},
                                       {.001, .002},
                                       {-.001, .003}}};
    std::vector<ExpectedProbe> result;
    for (std::uint32_t t = 0; t <= f.transforms.size(); ++t) {
        const auto &transform = t < f.transforms.size() ? f.transforms[t] : *f.final_transform;
        for (std::uint32_t i = 0; i < inputs.size(); ++i) {
            const std::uint64_t seed = UINT64_C(0x1234abcddeadbeef) + i;
            const std::uint64_t stream = UINT64_C(0xfedc012300000000) + t;
            // The probe interface stores input in binary32. Compare the CPU
            // formula with that exact input while retaining the original model.
            Orbit input{{float(inputs[i].x), float(inputs[i].y)}, .375};
            const bool hasHistory = std::any_of(transform.variations.begin(), transform.variations.end(),
                [](const Term &term) {
                    return term.weight && (term.id == Variation::pre_blur ||
                           term.id == Variation::gaussian_blur || term.id == Variation::radial_blur);
                });
            const auto repeats = hasHistory ? 8u : 1u;
            const auto record = gpu::make_probe(t, input, seed, stream, repeats);
            Rng rng(seed, stream);
            auto expected = input;
            for (std::uint32_t step = 0; step < repeats; ++step)
                expected = apply_transform(transform, expected, rng);
            result.push_back({record, expected, rng.unit(),
                              "transform " + std::to_string(t) + ", input " + std::to_string(i)});
        }
    }
    return result;
}

void formula_probes(const RenderOptions &options) {
    gpu::ContextScope contextScope;
    std::uint64_t comparisons = 0;
    double worst = 0;
    for (unsigned producer = 0; producer < 3; ++producer) {
        for (unsigned matrix = 0; matrix < 2; ++matrix) {
            Flame f;
            f.width = f.height = 8;
            f.source_version = producer == 0 ? "" : producer == 1 ? "Apophysis 2.09" : "Apophysis 7X";
            for (const auto &variation : variation_registry()) {
                Transform t;
                t.pre = matrix ? Affine{-.41, .63, .28, -.71, -.57, .26}
                               : Affine{.69, .21, -.37, .52, .33, -.46};
                t.post = {.91, -.13, .27, .75, -.05, .08};
                t.color = .625;
                t.color_speed = .375;
                t.parameters.apophysis_math = producer != 0;
                t.parameters.separate_blur_angle = producer == 2;
                t.parameters.julian_power = matrix ? -3 : 3;
                t.parameters.julian_dist = .75;
                auto &p = t.parameters;
                p.blob_low = -.3; p.blob_high = 1.25; p.blob_waves = producer ? 3 : 3.5;
                p.pdj_a = -.83; p.pdj_b = .57; p.pdj_c = 1.3; p.pdj_d = -.65;
                p.fan2_x = -.37; p.fan2_y = .19; p.rings2_val = .46;
                p.perspective_angle = -.23; p.perspective_dist = 2.5;
                p.juliascope_power = matrix ? -3 : 4; p.juliascope_dist = .73;
                p.radial_blur_angle = matrix ? -.38 : .49;
                p.pie_slices = 5; p.pie_rotation = -.45; p.pie_thickness = .28;
                p.ngon_sides = 5; p.ngon_power = .85; p.ngon_circle = .64; p.ngon_corners = .34;
                p.curl_c1 = -.32; p.curl_c2 = .16;
                p.splits_x = -.63; p.splits_y = .28;
                p.rectangles_x = matrix ? 0 : .39; p.rectangles_y = matrix ? -.55 : 0;
                const double weight = matrix ? -.375 : .625;
                t.variations = {{variation.id, weight}};
                if (variation.id == Variation::pre_blur)
                    t.variations.push_back({Variation::linear, 1});
                if (variation.id != Variation::julia)
                    t.variations.insert(t.variations.begin(), {Variation::julia, 0});
                f.transforms.push_back(t);
            }
            // The power ±1 fast paths in Apophysis consume no branch draw.
            // Generic flames consume one even for a single possible branch.
            for (double power : {1., -1.}) {
                auto t = f.transforms.front();
                t.variations = {{Variation::juliascope, -.75}};
                t.parameters.juliascope_power = power;
                t.parameters.juliascope_dist = 1;
                f.transforms.push_back(t);
            }
            for (unsigned i = 0; i < 2; ++i) {
                auto t = f.transforms[0];
                t.variations = {{Variation::pre_blur, .125},
                                {Variation::linear, .3},
                                {Variation::swirl, .1},
                                {Variation::julia, .25},
                                {Variation::noise, .1},
                                {Variation::gaussian_blur, .15},
                                {Variation::radial_blur, -.38}};
                if (i) {
                    std::swap(t.variations[3], t.variations[4]);
                    std::swap(t.variations[5], t.variations[6]);
                }
                f.transforms.push_back(t);
            }
            f.final_transform = f.transforms.back();
            const auto cases = build_probes(f);
            std::vector<std::uint32_t> raw;
            for (const auto &c : cases)
                raw.insert(raw.end(), c.input.begin(), c.input.end());
            const auto packed = gpu::pack_flame(f, {8, 8, 0, 1, 1, 0});
            const auto output = gpu::run_probes(packed, raw, options);
            check(output.size() == raw.size(), "GPU probe result size changed");
            for (std::size_t i = 0; i < cases.size(); ++i) {
                gpu::ProbeRecord record;
                std::copy_n(output.begin() + i * gpu::probe_words, gpu::probe_words, record.begin());
                const auto actual = gpu::probe_orbit(record);
                const auto &expected = cases[i].output;
                for (const auto &pair : {std::pair{actual.point.x, expected.point.x},
                                         std::pair{actual.point.y, expected.point.y},
                                         std::pair{actual.color, expected.color}}) {
                    const auto [a, e] = pair;
                    const double relative = std::abs(a - e) / std::max(1.0, std::abs(e));
                    worst = std::max(worst, relative);
                    if (!std::isfinite(a) || !std::isfinite(e) || relative > 2e-4) {
                        std::ostringstream error;
                        error << std::setprecision(17) << "GPU formula mismatch producer " << producer
                              << ", " << cases[i].description << ": actual=" << a << " expected=" << e
                              << " normalized error=" << relative;
                        throw std::runtime_error(error.str());
                    }
                }
                check(gpu::probe_next_unit(record) == cases[i].next_unit,
                      "GPU random draw order mismatch: " + cases[i].description);
                ++comparisons;
            }
        }
    }
    std::cout << comparisons
              << " direct CPU/GPU transform probes passed; maximum normalized coordinate error="
              << std::setprecision(8) << worst << " (limit 0.0002), all subsequent RNG draws exact\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool cpu_only = false, require_vulkan = false;
        std::string requested_device;
        for (int i = 1; i < argc; ++i) {
            const std::string arg(argv[i]);
            if (arg == "--cpu-only")
                cpu_only = true;
            else if (arg == "--require-vulkan")
                require_vulkan = true;
            else if (arg == "--device") {
                if (++i == argc)
                    throw std::invalid_argument("--device requires a discovery ID or index");
                requested_device = argv[i];
            } else
                throw std::invalid_argument("Unknown GPU kernel test argument: " + arg);
        }
        portable_streams();
        packing_and_admission();
        if (cpu_only)
            return 0;
        const auto info = compute_devices();
        unsigned index = UINT_MAX;
        if (!requested_device.empty()) {
            const auto parsed = std::from_chars(
                requested_device.data(), requested_device.data() + requested_device.size(), index);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != requested_device.data() + requested_device.size())
                index = UINT_MAX;
        }
        const auto found =
            std::find_if(info.devices.begin(), info.devices.end(), [&](const ComputeDevice &d) {
                return d.usable &&
                       (requested_device.empty() || d.id == requested_device || d.index == index);
            });
        if (found == info.devices.end()) {
            std::cout << "No usable Vulkan device: " << info.diagnostic << '\n';
            return require_vulkan || !requested_device.empty() ? 1 : 77;
        }
        RenderOptions options;
        options.backend = RenderBackend::vulkan;
        options.device = found->id;
        options.allow_cpu_fallback = false;
        options.memory_mib = options.gpu_memory_mib = 128;
        std::cout << "Formula probe device: " << found->name
                  << (found->software ? " [software Vulkan]" : " [GPU]") << '\n';
        formula_probes(options);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "GPU kernel tests: " << e.what() << '\n';
        return 1;
    }
}
