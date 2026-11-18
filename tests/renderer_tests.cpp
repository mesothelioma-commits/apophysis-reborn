#include "render.hpp"
#include "output_file.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stop_token>
#include <stdexcept>

namespace {
using namespace apo;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
Flame point_flame() {
    Flame f;
    f.width = 13;
    f.height = 9;
    f.scale = 2;
    f.gamma = 1;
    f.brightness = .12;
    f.palette.fill({1, 0, 0});
    Transform t;
    t.pre = {0, 0, 0, 0, 1, 0};
    t.color_speed = 1;
    f.transforms.push_back(t);
    return f;
}
std::array<unsigned, 3> pixel(const Image& img, int x, int y) {
    const auto i = (std::size_t(y) * img.width + x) * 3;
    return {img.rgb.at(i), img.rgb.at(i + 1), img.rgb.at(i + 2)};
}
void pixel_at(const Image& img, int x, int y, std::array<unsigned, 3> rgb) {
    for (int j = 0; j < img.height; ++j)
        for (int i = 0; i < img.width; ++i)
            require(pixel(img, i, j) == ((x == i && y == j) ? rgb : std::array<unsigned, 3>{}),
                    "unexpected geometry, tone or background");
}
void test_projection_and_color() {
    auto f = point_flame();
    RenderOptions opt{18001, 72, 3};
    // A repeated point has integral mass N. For ppu=2, exposure sees log(1+4).
    const auto level = [](double gain) {
        return unsigned(std::lround(255 * .12 * (268./256) * std::log1p(gain)));
    };
    auto result = render(f, opt);
    require(result.plotted == opt.samples && !result.invalid, "incorrect sample count");
    pixel_at(result.image, 8, 4, {level(4), 0, 0});
    f.center = {1, -1};
    f.rotate = 90;
    pixel_at(render(f, opt).image, 4, 4, {level(4), 0, 0});
    f.center = {};
    pixel_at(render(f, opt).image, 6, 6, {level(4), 0, 0});

    f.rotate = 0;
    f.final_transform.emplace();
    f.final_transform->post.e = 1;
    f.final_transform->color = 1;
    f.final_transform->color_speed = 1;
    f.palette.back() = {0, 0, 1};
    pixel_at(render(f, opt).image, 10, 4, {0, 0, level(4)});
    f.final_transform->opacity = 0;
    pixel_at(render(f, opt).image, 8, 4, {level(4), 0, 0});
    f.final_transform.reset();
    f.transforms[0].opacity = .5;
    pixel_at(render(f, opt).image, 8, 4, {level(.4), 0, 0});
    f.transforms[0].opacity = 0;
    f.background = {0, .2, 1};
    result = render(f, opt);
    require(result.plotted == 0 && pixel(result.image, 8, 4) == std::array<unsigned, 3>{0, 51, 255},
            "invisible iterations must leave the background");
    f = point_flame();
    f.transforms[0].color = 1./512;
    f.palette[1] = {0,0,1};
    f.palette_mode = PaletteMode::linear;
    const unsigned half = unsigned(std::lround(255 * .06 * (268./256) * std::log(5.)));
    pixel_at(render(f, opt).image, 8, 4, {half, 0, half});

    f = point_flame();
    f.oversample = 2;
    const unsigned filtered = unsigned(std::lround(255 * .12 * (268./256) / 4 * std::log(17.)));
    pixel_at(render(f, opt).image, 8, 4, {filtered, 0, 0});
}
void test_final_feedback_and_worker_order() {
    auto f = point_flame();
    // Identity orbit would drift out of view if the final translation fed back.
    f.transforms[0].pre = {.5, 0, 0, .5, 0, 0};
    f.final_transform.emplace();
    f.final_transform->post.e = 2;
    RenderOptions opt{25000, 13, 4};
    auto result = render(f, opt);
    require(result.plotted == opt.samples && pixel(result.image, 10, 4)[0] > 0,
            "final transformation must affect plotting only");
    f = demo_flame();
    f.width = f.height = 97;
    f.scale = 38;
    f.transforms[0].chaos = {0, 4, 1};
    opt.threads = 1;
    auto one = render(f, opt);
    opt.threads = 4;
    auto four = render(f, opt);
    require(four.workers == 4 && four.plotted == one.plotted && four.image.rgb == one.image.rgb,
            "worker scheduling changed a seeded image");
    ++opt.seed;
    require(render(f, opt).image.rgb != one.image.rgb, "seed had no effect on stochastic geometry");
    f.transforms.resize(1);
    f.transforms[0].chaos.clear();
    f.transforms[0].pre = {0, 0, 0, 0, 0, 0};
    f.transforms[0].variations = {{Variation::diamond, 1}};
    auto singular = render(f, opt);
    require(singular.invalid >= opt.samples && !singular.plotted, "non-finite orbit recovery");
}
void test_legacy_reconstruction() {
    auto f = point_flame();
    f.brightness = .8;
    f.filter_radius = 1.3;
    const RenderOptions opt{45000, 35, 3};
    auto image = render(f, opt).image;
    // Each row is the rounded central scanline of a unit impulse after log-density.
    // The historical implementations use different Gaussian cutoffs and support rounding.
    const auto scan = [&](const Image& im, int x, int y, const std::array<unsigned,5>& values) {
        for (std::size_t i = 0; i < values.size(); ++i)
            require(pixel(im,x+int(i)-2,y)[0] == values[i], "Gaussian impulse reconstruction");
    };
    scan(image,8,4,{1,40,129,40,1});
    f.source_version = "Apophysis 2.09";
    scan(render(f,opt).image,8,4,{0,35,175,35,0});
    f.source_version.clear();
    f.transforms[0].pre.e = -3.5; // Point lies half a pixel outside the left edge.
    require(pixel(render(f,opt).image,0,4)[0] == 40,"filter must sample the outside gutter");
    f = point_flame();
    f.brightness = .015;
    f.gamma = 2;
    f.gamma_threshold = .16;
    const double density = (268./256)*std::log(5.)*f.brightness;
    const double t = density/.16;
    const auto adjusted = std::lround(255*((1-t)*t*.4 + t*std::sqrt(density)));
    require(pixel(render(f,opt).image,8,4)[0] == static_cast<unsigned>(adjusted), "gamma knee");
    f = point_flame();
    f.source_version = "Apophysis 7x";
    f.transforms[0].opacity = .5;
    auto result = render(f,opt);
    require(result.plotted > opt.samples*.48 && result.plotted < opt.samples*.52,
            "Apophysis opacity is a plot probability");
    f.transforms[0].opacity = 1;
    f.transforms[0].color = .6/255;
    f.palette[1] = {0,1,0};
    require(pixel(render(f,opt).image,8,4)[1] != 0, "historical nearest palette index");
    f.filter_radius = 1e300;
    try { render(f,opt); throw std::runtime_error("Unrepresentable filter footprint"); }
    catch (const std::invalid_argument&) {}
}
void test_cancellation_and_progress() {
    auto f = point_flame();
    std::stop_source source;
    RenderOptions opt{100000, 13, 3};
    opt.stop_token = source.get_token();
    std::uint64_t last = 0;
    opt.progress = [&](std::uint64_t completed, std::uint64_t total) {
        require(total == opt.samples && completed >= last && completed <= total,
                "progress must report monotonic committed sample counts");
        last = completed;
    };
    auto completed = render(f, opt);
    require(last == opt.samples && completed.plotted == opt.samples, "complete render progress");
    source.request_stop();
    try { render(f, opt); throw std::runtime_error("pre-cancelled render ran"); }
    catch (const RenderCancelled&) {}

    source = std::stop_source();
    opt.stop_token = source.get_token();
    opt.progress = [&](std::uint64_t completed, std::uint64_t) {
        if (completed >= 8192) source.request_stop();
    };
    try { render(f, opt); throw std::runtime_error("render did not cancel between batches"); }
    catch (const RenderCancelled&) {}

    const auto folder = std::filesystem::temp_directory_path() /
        ("apophysis-cancel-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const auto output = folder / "existing.ppm";
    {
        std::ofstream out(output);
        out << "original";
    }
    try { write_image(output, completed.image, false, source.get_token());
          throw std::runtime_error("cancelled output overwrote file"); }
    catch (const RenderCancelled&) {}
    try {
        write_atomic(output, [](std::ostream& out) { out << "replacement"; },
                     [] { throw RenderCancelled(); });
        throw std::runtime_error("cancelled commit overwrote file");
    } catch (const RenderCancelled&) {}
    std::ifstream in(output);
    std::string contents;
    in >> contents;
    require(contents == "original", "failed/cancelled output changed the destination");
    in.close();
    require(std::distance(std::filesystem::directory_iterator(folder),
                          std::filesystem::directory_iterator()) == 1,
            "cancelled output left a staged file");
    std::filesystem::remove_all(folder);
}
void test_chaos_selector_budget() {
    auto f = point_flame();
    f.transforms.resize(2048, f.transforms.front());
    RenderOptions options{64, 19, 1, 1};
    const auto ordinary_bytes = TransformChooser::storage_bytes(f);
    require(render(f, options).plotted == options.samples,
            "ordinary large transform list should use a shared distribution");
    for (auto& transform : f.transforms) transform.chaos = {1, 1};
    require(TransformChooser::storage_bytes(f) == ordinary_bytes,
            "all-one chaos rows must reuse the ordinary distribution");
    for (auto& transform : f.transforms) transform.chaos = {0};
    require(TransformChooser::storage_bytes(f) > 32 * 1024 * 1024,
            "nondefault chaos table estimate omitted per-row distributions");
    bool sampled = false;
    options.progress = [&](std::uint64_t, std::uint64_t) { sampled = true; };
    try {
        render(f, options);
        throw std::runtime_error("chaos tables escaped the renderer memory budget");
    } catch (const std::invalid_argument& error) {
        require(std::string(error.what()).find("selection tables") != std::string::npos,
                "large chaos model failed for an unrelated reason");
    }
    require(!sampled, "over-budget selector reached sampling");
    f.transforms.front().weight = 0;
    const auto without_unreachable = TransformChooser::storage_bytes(f);
    f.transforms.front().chaos.clear();
    require(TransformChooser::storage_bytes(f) == without_unreachable,
            "unreachable transform must not allocate a chaos distribution");
}

void test_limits() {
    auto f = point_flame();
    f.width = f.height = 16384;
    try { render(f, {1, 1, 1, 8}); throw std::runtime_error("memory budget not enforced"); }
    catch (const std::invalid_argument&) {}
    f = point_flame();
    f.quality = .25;
    f.zoom = 1;
    require(default_samples(f) == std::uint64_t(f.width*f.height), "quality and zoom sample budget");
    try { render(f, {0, 0, 1}); throw std::runtime_error("zero samples permitted"); }
    catch (const std::invalid_argument&) {}
}
}

int main() {
    try {
        test_projection_and_color();
        test_final_feedback_and_worker_order();
        test_limits();
        test_chaos_selector_budget();
        test_legacy_reconstruction();
        test_cancellation_and_progress();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
