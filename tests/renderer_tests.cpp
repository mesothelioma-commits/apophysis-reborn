#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
