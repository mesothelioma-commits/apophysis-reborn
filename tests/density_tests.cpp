// Controlled comparisons to the original FLAM3 64-bit density pass.
#include "flame_io.hpp"
#include "legacy_document.hpp"
#include "render_internal.hpp"
#include "render_file.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace {
using namespace apo;
namespace fs = std::filesystem;

void check(bool ok, std::string_view note) {
    if (!ok) throw std::runtime_error(std::string(note));
}
std::vector<std::uint8_t> ppm_pixels(const fs::path& path, int width, int height) {
    std::ifstream stream(path, std::ios::binary);
    std::string magic;
    int w = 0, h = 0, maximum = 0;
    stream >> magic >> w >> h >> maximum;
    check(magic == "P6" && maximum == 255 && width == w && height == h && stream.get() == '\n', "Expected a complete RGB8 PPM");
    std::vector<std::uint8_t> rgb(std::istreambuf_iterator<char>(stream), {});
    check(rgb.size() == std::size_t(width)*height*3, "Truncated PPM");
    return rgb;
}
void compare_stage(const fs::path& fixture, Flame flame) {
    std::ifstream data(fixture);
    check(bool(data), "Missing upstream density vectors");
    constexpr std::uint64_t quantum = (1u << 24) - 1;
    std::string tag, name;
    unsigned cases = 0;
    std::size_t verified = 0;
    while (data >> tag) {
        check(tag == "case", "Malformed density oracle case");
        unsigned width, height;
        DensityEstimator de;
        RenderOptions options;
        data >> name >> width >> height >> flame.oversample >> de.radius >> de.minimum >> de.curve >> options.samples >> flame.scale;
        check(width == 39 && height == 33, "Invalid test geometry");
        flame.density_estimator = de;
        auto geometry = detail::render_geometry(flame, options);
        // The original generator takes its histogram directly, independent of camera or sampling.
        geometry.gpu.width = width;
        geometry.gpu.height = height;
        std::vector<detail::Bin> bins(std::size_t(width)*height);
        std::vector<detail::LightBin> expected(bins.size());
        while (data >> tag && tag != "end") {
            unsigned x, y;
            data >> x >> y;
            check(x < width && y < height, "Malformed oracle coordinate");
            const auto at = std::size_t(y)*width + x;
            if (tag == "bin") {
                unsigned hits, r, g, b;
                data >> hits >> r >> g >> b;
                const auto unit = quantum / 255 * hits;
                bins[at] = {r*unit,g*unit,b*unit,255*unit};
            } else {
                check(tag == "light", "Unexpected density oracle row");
                for (auto& channel : expected[at]) data >> channel;
            }
        }
        check(data.good() && tag == "end", "Truncated density oracle case");
        const auto actual = detail::density_strip(flame, geometry, bins, 0, 0, height, options.samples, {});
        check(actual.size() == expected.size(), "Incorrect stage dimensions");
        for (std::size_t i = 0; i < actual.size(); ++i) {
            for (unsigned c = 0; c < 4; ++c) {
                if (!(std::abs(actual[i][c] - expected[i][c]) <= 1e-12 * std::max(1., std::abs(expected[i][c])))) {
                    std::ostringstream note;
                    note << name << " (" << i % width << "," << i / width << ") channel " << c
                         << " expected " << expected[i][c] << " from FLAM3, got " << actual[i][c];
                    throw std::runtime_error(note.str());
                }
                ++verified;
            }
        }
        ++cases;
    }
    check(cases == 5 && verified == 25740, "Incomplete FLAM3 comparison");
    std::cout << verified << " linear-light values matched FLAM3 across five histograms\n";
}

void model_edits(const fs::path& example) {
    const auto original = LegacyDocument::load(example);
    const auto model = original.render_model(0);
    check(model.density_estimator == DensityEstimator{3.5,.25,.4}, "Density settings missing from imported study");
    auto edited = original;
    LegacyEdits change;
    change.density_estimator = DensityEstimator{2,.1,.6};
    edited.edit(0, change);
    check(edited.render_model(0).density_estimator == change.density_estimator, "Source edit lost density settings");
    check(parse_flames(serialize_flame(edited.render_model(0))).at(0).density_estimator == change.density_estimator,
          "Canonical export lost active density settings");
    change = {};
    change.disable_density_estimator = true;
    edited.edit(0, change);
    check(!edited.render_model(0).density_estimator, "Disable edit reactivated a producer default");
    check(!parse_flames(serialize_flame(edited.render_model(0))).at(0).density_estimator, "Canonical disable was lost");
    change = {};
    change.density_estimator = DensityEstimator{1,2,.4};
    const auto saved = edited.bytes();
    bool rejected = false;
    try { edited.edit(0, change); } catch (const std::exception&) { rejected = true; }
    check(rejected && saved == edited.bytes(), "Invalid density edit changed source bytes");
}

void full_image(const fs::path& fixtures, const fs::path& work) {
    const auto impulse = load_flames(fixtures / "density-impulse.flame").at(0);
    const auto expected = ppm_pixels(fixtures / "density-impulse.ppm", impulse.width, impulse.height);
    RenderOptions options;
    options.threads = 1; options.seed = 27; options.samples = default_samples(impulse);
    const auto result = render(impulse, options);
    check(result.image.rgb.size() == expected.size(), "Original full-render dimensions differ");
    unsigned max_error = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        max_error = std::max(max_error, unsigned(std::abs(int(result.image.rgb[i]) - int(expected[i]))));
    std::cout << "Complete FLAM3 impulse image, largest 8-bit channel difference: " << max_error << "\n";
    check(max_error <= 1, "Controlled impulse differs from the complete original FLAM3 render");
    auto off = impulse;
    off.density_estimator.reset();
    check(render(off, options).image.rgb != result.image.rgb, "Density switch did not change rendered pixels");

    FileRenderOptions file;
    file.image.format = ImageFormat::ppm8;
    file.strip_rows = 1;
    const auto output = work / "one-row.ppm";
    render_to_file(impulse, options, output, file);
    check(ppm_pixels(output, impulse.width, impulse.height) == result.image.rgb, "One-row strips changed density halo pixels");
}

void strip_and_cancel(const fs::path& example, const fs::path& work) {
    auto study = load_flames(example).at(0);
    study.width = 68; study.height = 49; study.oversample = 2; study.scale = 19;
    RenderOptions options;
    options.threads = 2; options.samples = 40000; options.seed = 317;
    const auto image = render(study,options).image;
    FileRenderOptions file;
    file.image.format = ImageFormat::ppm8; file.strip_rows = 3;
    const auto output = work / "study.ppm";
    render_to_file(study, options, output, file);
    check(image.rgb == ppm_pixels(output, study.width, study.height), "Active-density image and streamed strips disagree");
    // A different prior file makes premature publication observable.
    const std::string sentinel = "previous output from another job";
    { std::ofstream previous(output, std::ios::binary); previous << sentinel; }
    std::stop_source cancel;
    options.stop_token = cancel.get_token();
    file.status = [&](const FileRenderProgress& p) { if (p.phase == "Reconstructing") cancel.request_stop(); };
    bool cancelled = false;
    try { render_to_file(study, options, output, file); } catch (const RenderCancelled&) { cancelled = true; }
    std::ifstream remaining(output, std::ios::binary);
    const std::string bytes(std::istreambuf_iterator<char>(remaining), {});
    check(cancelled && bytes == sentinel, "Cancelled density job replaced an existing render");
}

} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected density-oracle.tsv and density-study.flame paths");
        const fs::path vectors = argv[1], study = argv[2];
        const auto work = fs::temp_directory_path() /
            ("reborn-density-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(fs::create_directory(work), "Could not reserve test output folder");
        try {
            model_edits(study);
            compare_stage(vectors, load_flames(study).at(0));
            full_image(vectors.parent_path(), work);
            strip_and_cancel(study, work);
        } catch (...) {
            fs::remove_all(work);
            throw;
        }
        fs::remove_all(work);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
