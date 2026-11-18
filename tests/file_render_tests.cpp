#include "gpu.hpp"
#include "render_file.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string_view>

namespace {
using namespace apo;
namespace fs = std::filesystem;
constexpr std::uint64_t MiB = 1024 * 1024;
void require(bool value, std::string_view message) {
    if (!value)
        throw std::runtime_error(std::string(message));
}
template <class Exception = std::exception, class F>
std::string rejects(F &&action, std::string_view message) {
    try {
        action();
    } catch (const Exception &error) {
        return error.what();
    }
    throw std::runtime_error(std::string(message));
}
std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot read test output");
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const fs::path &path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(bool(out), "Cannot write test fixture");
}
std::uint64_t le(const std::string &bytes, std::size_t at, unsigned n = 4) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i)
        value |= std::uint64_t(static_cast<unsigned char>(bytes.at(at + i))) << (8 * i);
    return value;
}
struct Decoded {
    unsigned width = 0, height = 0, bits = 0, channels = 0;
    std::vector<std::uint16_t> samples;
};
Decoded ppm(const fs::path &path) {
    const auto bytes = read(path);
    std::istringstream header(bytes);
    std::string magic;
    unsigned w = 0, h = 0, maxval = 0;
    header >> magic >> w >> h >> maxval;
    require(magic == "P6" && (maxval == 255 || maxval == 65535) && header.get() == '\n',
            "Invalid PPM output header");
    const auto at = static_cast<std::size_t>(header.tellg());
    const unsigned size = maxval == 255 ? 1 : 2;
    const auto count = std::size_t(w) * h * 3;
    require(bytes.size() == at + count * size, "PPM output size mismatch");
    Decoded result{w, h, size * 8, 3, {}};
    result.samples.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto first = static_cast<unsigned char>(bytes[at + i * size]);
        result.samples.push_back(
            size == 1
                ? first
                : std::uint16_t(first * 256u + static_cast<unsigned char>(bytes[at + i * size + 1])));
    }
    return result;
}
Decoded tiff(const fs::path &path) {
    const auto bytes = read(path);
    require(bytes.substr(0, 2) == "II" && le(bytes, 2, 2) == 42, "Invalid TIFF output header");
    const auto directory = le(bytes, 4), count = le(bytes, directory, 2);
    std::map<unsigned, std::vector<std::uint32_t>> fields;
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = directory + 2 + i * 12;
        const auto tag = unsigned(le(bytes, at, 2)), type = unsigned(le(bytes, at + 2, 2));
        if (type != 3 && type != 4)
            continue;
        const auto n = le(bytes, at + 4);
        const auto size = type == 3 ? 2u : 4u;
        const auto begin = n * size <= 4 ? at + 8 : le(bytes, at + 8);
        auto &values = fields[tag];
        for (std::size_t j = 0; j < n; ++j)
            values.push_back(static_cast<std::uint32_t>(le(bytes, begin + j * size, size)));
    }
    Decoded result{
        fields.at(256).at(0), fields.at(257).at(0), fields.at(258).at(0), fields.at(277).at(0), {}};
    require(fields.at(259).at(0) == 1 && fields.at(262).at(0) == 2 && fields.at(274).at(0) == 1 &&
                fields.at(284).at(0) == 1,
            "TIFF must be uncompressed interleaved top-down RGB");
    if (result.channels == 4)
        require(fields.at(338).at(0) == 2, "TIFF alpha is not straight");
    const auto &offsets = fields.at(273);
    const auto &sizes = fields.at(279);
    require(offsets.size() == sizes.size(), "TIFF strip directory mismatch");
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        require(std::uint64_t(offsets[i]) + sizes[i] <= bytes.size(), "Truncated TIFF strip");
        for (std::size_t j = 0; j < sizes[i]; j += result.bits / 8)
            result.samples.push_back(
                static_cast<std::uint16_t>(le(bytes, offsets[i] + j, result.bits / 8)));
    }
    require(result.samples.size() == std::size_t(result.width) * result.height * result.channels,
            "TIFF sample count mismatch");
    return result;
}
struct Temporary {
    fs::path directory;
    Temporary() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = fs::temp_directory_path() / ("apo-file-tests-" + std::to_string(tick));
        require(fs::create_directory(directory), "Cannot create test directory");
    }
    ~Temporary() {
        std::error_code error;
        fs::remove_all(directory, error);
    }
};
Flame fixture(std::string source = {}) {
    auto flame = demo_flame();
    flame.name = "Streaming fixture & <metadata>";
    flame.source_version = std::move(source);
    flame.width = 43;
    flame.height = 31;
    flame.scale = 11.25;
    flame.rotate = 13.5;
    flame.center = {.13, -.07};
    flame.oversample = 2;
    flame.filter_radius = .5;
    flame.brightness = .8;
    flame.vibrancy = .75;
    flame.gamma_threshold = .03;
    flame.background = {.13, .27, .03};
    flame.palette_mode = PaletteMode::linear;
    for (std::size_t i = 0; i < flame.transforms.size(); ++i) {
        auto &t = flame.transforms[i];
        t.opacity = .5 + double(i) * .125;
        t.chaos = {1, 2, 0};
        t.parameters.apophysis_math = !flame.source_version.empty();
        t.parameters.separate_blur_angle = flame.source_version == "Apophysis 7x";
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
RenderOptions options(std::uint64_t samples = 16781) {
    RenderOptions o;
    o.samples = samples;
    o.seed = 0x138ec785a64132b9ULL;
    o.threads = 1;
    o.memory_mib = 64;
    return o;
}
FileRenderOptions file_options(unsigned rows = 7) {
    FileRenderOptions file;
    file.image.format = ImageFormat::ppm8;
    file.strip_rows = rows;
    file.disk_mib = 64;
    return file;
}
void pixels_equal(const Decoded &decoded, const RenderResult &dense, std::string_view message) {
    require(decoded.width == unsigned(dense.image.width) &&
                decoded.height == unsigned(dense.image.height) && decoded.bits == 8 &&
                decoded.channels == 3,
            "Dense/stream dimensions differ");
    require(decoded.samples.size() == dense.image.rgb.size(), "Dense/stream raster sizes differ");
    require(std::equal(decoded.samples.begin(), decoded.samples.end(), dense.image.rgb.begin()),
            message);
}
void cpu_exact(const fs::path &directory) {
    struct Case {
        const char *source;
        int oversample;
        double radius;
    };
    const std::array cases{Case{"", 1, -1},
                           Case{"", 3, -1},
                           Case{"", 2, .5},
                           Case{"", 2, .01},
                           Case{"", 3, .01},
                           Case{"", 2, 1.25},
                           Case{"Apophysis 2.09", 2, .5},
                           Case{"Apophysis 2.09", 3, .01},
                           Case{"Apophysis 7x", 3, .65},
                           Case{"Apophysis 7x", 2, .01}};
    unsigned index = 0;
    for (const auto &test : cases) {
        auto flame = fixture(test.source);
        flame.oversample = test.oversample;
        if (test.radius < 0)
            flame.filter_radius.reset();
        else
            flame.filter_radius = test.radius;
        const auto dense = render(flame, options());
        for (auto rows : {1u, 7u, unsigned(flame.height)}) {
            auto opt = options();
            opt.threads = rows == 7 ? 3 : 1;
            const auto path = directory / ("exact-" + std::to_string(index++) + ".ppm");
            const auto result = render_to_file(flame, opt, path, file_options(rows));
            pixels_equal(ppm(path), dense,
                         "CPU stream pixels changed with strip height or worker count");
            require(result.plotted == dense.plotted && result.invalid == dense.invalid &&
                        result.backend == RenderBackend::cpu,
                    "CPU stream counters differ from dense");
        }
    }
    std::cout << "CPU: 30 exact dense/strip comparisons, box/Gaussian/parity/2.09/7X/worker counts\n";
}
void checkpoint_tests(const fs::path &directory) {
    const auto flame = fixture("Apophysis 7x");
    auto opt = options(65539);
    const auto expected = render(flame, opt);
    auto file = file_options();
    file.checkpoint_directory = directory / "paused";
    const auto destination = directory / "preserved.ppm";
    write(destination, "existing destination");
    std::stop_source stopped;
    opt.stop_token = stopped.get_token();
    std::uint64_t prior = 0, committed = 0;
    opt.progress = [&](auto completed, auto total) {
        require(total == opt.samples && completed >= prior, "Nonmonotonic committed progress");
        prior = completed;
        if (completed) {
            require(has_render_checkpoint(file.checkpoint_directory),
                    "Progress preceded checkpoint commit");
            committed = completed;
            stopped.request_stop();
        }
    };
    rejects<RenderCancelled>([&] { render_to_file(flame, opt, destination, file); },
                             "Pause did not cancel");
    require(committed == 65536 && read(destination) == "existing destination" &&
                has_render_checkpoint(file.checkpoint_directory),
            "Pause lost checkpoint or original output");
    opt.stop_token = {};
    opt.progress = {};
    const auto saved = file.checkpoint_directory;
    auto clone = [&](std::string name) {
        auto copied = file;
        copied.resume = true;
        copied.checkpoint_directory = directory / name;
        fs::copy(saved, copied.checkpoint_directory, fs::copy_options::recursive);
        return copied;
    };
    auto bad_resume = [&](const Flame &f, const RenderOptions &o, const FileRenderOptions &fi,
                          std::string_view message) {
        rejects([&] { render_to_file(f, o, destination, fi); }, message);
        require(read(destination) == "existing destination", "Rejected resume damaged destination");
    };
    auto changed_file = clone("changed");
    auto changed = flame;
    changed.gamma += .01;
    bad_resume(changed, opt, changed_file, "Changed model accepted");
    changed = flame;
    changed.transforms[0].parameters.apophysis_math = !changed.transforms[0].parameters.apophysis_math;
    bad_resume(changed, opt, changed_file, "Changed historical math flag accepted");
    changed = flame;
    changed.final_transform->parameters.separate_blur_angle =
        !changed.final_transform->parameters.separate_blur_angle;
    bad_resume(changed, opt, changed_file, "Changed final transform RNG flag accepted");
    auto changed_opt = opt;
    ++changed_opt.samples;
    bad_resume(flame, changed_opt, changed_file, "Changed sample target accepted");
    changed_opt = opt;
    ++changed_opt.seed;
    bad_resume(flame, changed_opt, changed_file, "Changed seed accepted");
    auto format = changed_file;
    format.image.format = ImageFormat::ppm16;
    bad_resume(flame, opt, format, "Changed export depth accepted");
    format = changed_file;
    ++format.strip_rows;
    bad_resume(flame, opt, format, "Changed checkpoint strip height accepted");
    auto corrupt = clone("corrupt-manifest");
    auto bytes = read(corrupt.checkpoint_directory / "manifest.apocheckpoint");
    const auto cpu = bytes.find("CPU");
    require(cpu != std::string::npos, "No CPU identity in manifest");
    bytes[cpu] = 'X';
    write(corrupt.checkpoint_directory / "manifest.apocheckpoint", bytes);
    require(!has_render_checkpoint(corrupt.checkpoint_directory), "Corrupted manifest passed probe");
    bad_resume(flame, opt, corrupt, "Corrupted manifest accepted");
    corrupt = clone("truncated-manifest");
    const auto manifest = corrupt.checkpoint_directory / "manifest.apocheckpoint";
    fs::resize_file(manifest, fs::file_size(manifest) / 2);
    require(!has_render_checkpoint(corrupt.checkpoint_directory), "Truncated manifest passed probe");
    bad_resume(flame, opt, corrupt, "Truncated manifest accepted");
    corrupt = clone("corrupt-payload");
    auto chunk = corrupt.checkpoint_directory / "chunk-0.apohits";
    bytes = read(chunk);
    const auto payload = 64 + le(bytes, 40, 8) * 24;
    require(payload + 24 <= bytes.size(), "Fixture has no checkpoint records");
    bytes[static_cast<std::size_t>(payload) + 8] ^= 1;
    write(chunk, bytes);
    bad_resume(flame, opt, corrupt, "Corrupted payload checksum accepted");
    corrupt = clone("truncated-chunk");
    chunk = corrupt.checkpoint_directory / "chunk-0.apohits";
    fs::resize_file(chunk, fs::file_size(chunk) - 1);
    require(!has_render_checkpoint(corrupt.checkpoint_directory), "Truncated chunk passed probe");
    bad_resume(flame, opt, corrupt, "Truncated chunk accepted");
    corrupt = clone("missing-chunk");
    fs::remove(corrupt.checkpoint_directory / "chunk-0.apohits");
    require(!has_render_checkpoint(corrupt.checkpoint_directory), "Missing chunk passed probe");
    bad_resume(flame, opt, corrupt, "Missing chunk accepted");

    auto orphan = clone("orphan");
    write(orphan.checkpoint_directory / "chunk-1.apohits", "uncommitted interrupted write");
    write(orphan.checkpoint_directory / "chunk-999.apohits", "unreferenced orphan");
    require(has_render_checkpoint(orphan.checkpoint_directory),
            "Orphans invalidated committed journal");
    const auto orphan_output = directory / "orphan.ppm";
    opt.threads = 3;
    opt.memory_mib = 32;
    require(render_to_file(flame, opt, orphan_output, orphan).resumed, "Resume flag omitted");
    pixels_equal(ppm(orphan_output), expected, "Orphan recovery changed final pixels");
    file.resume = true;
    const auto resumed = render_to_file(flame, opt, destination, file);
    require(resumed.resumed && resumed.plotted == expected.plotted &&
                resumed.invalid == expected.invalid,
            "Resumed counters differ");
    pixels_equal(ppm(destination), expected, "Resumed pixels differ from a fresh dense job");

    auto locked = file_options();
    locked.checkpoint_directory = directory / "locked";
    std::stop_source lock_stop;
    opt.stop_token = lock_stop.get_token();
    bool locked_out = false;
    opt.progress = [&](auto completed, auto) {
        if (!completed)
            return;
        auto concurrent = locked;
        concurrent.resume = true;
        auto other = options(65539);
        const auto error =
            rejects([&] { render_to_file(flame, other, directory / "concurrent.ppm", concurrent); },
                    "A second same-process handle acquired the checkpoint lock");
        require(error.find("locked") != std::string::npos || error.find("in use") != std::string::npos,
                "Concurrent checkpoint was rejected for the wrong reason");
        locked_out = true;
        lock_stop.request_stop();
    };
    rejects<RenderCancelled>([&] { render_to_file(flame, opt, directory / "locked.ppm", locked); },
                             "Lock fixture did not pause");
    require(locked_out && has_render_checkpoint(locked.checkpoint_directory), "Lock test lost journal");
    opt.stop_token = {};
    opt.progress = {};
    locked.resume = true;
    render_to_file(flame, opt, directory / "unlocked.ppm", locked);
    pixels_equal(ppm(directory / "unlocked.ppm"), expected, "Lock was not released after cancellation");

    auto reconstruction = file_options(1);
    reconstruction.checkpoint_directory = directory / "reconstruction";
    const auto preserved = directory / "reconstruction.ppm";
    write(preserved, "retain during reconstruction");
    std::stop_source reconstruct_stop;
    opt.stop_token = reconstruct_stop.get_token();
    reconstruction.status = [&](const FileRenderProgress &status) {
        if (status.phase == "Reconstructing" && status.completed_strips == 1)
            reconstruct_stop.request_stop();
    };
    rejects<RenderCancelled>([&] { render_to_file(flame, opt, preserved, reconstruction); },
                             "Reconstruction cancellation ignored");
    require(read(preserved) == "retain during reconstruction" &&
                has_render_checkpoint(reconstruction.checkpoint_directory),
            "Reconstruction cancellation lost old output or checkpoint");
    reconstruction.resume = true;
    reconstruction.status = {};
    opt.stop_token = {};
    unsigned progress_calls = 0;
    opt.progress = [&](auto n, auto total) {
        require(n == total, "Completed checkpoint resampled");
        ++progress_calls;
    };
    render_to_file(flame, opt, preserved, reconstruction);
    require(progress_calls == 1, "Completed checkpoint unexpectedly sampled again");
    pixels_equal(ppm(preserved), expected, "Reconstructed resume changed pixels");
    opt.progress = {};

    const auto safe_manifest = read(file.checkpoint_directory / "manifest.apocheckpoint");
    rejects(
        [&] { render_to_file(flame, opt, file.checkpoint_directory / "manifest.apocheckpoint", file); },
        "Output was allowed to overwrite its checkpoint manifest");
    fs::create_directory(file.checkpoint_directory / "child");
    rejects(
        [&] { render_to_file(flame, opt, file.checkpoint_directory / "child" / "output.ppm", file); },
        "Output inside a checkpoint descendant was admitted");
    std::error_code symlink_error;
    const auto alias = directory / "checkpoint-alias";
    fs::create_directory_symlink(file.checkpoint_directory, alias, symlink_error);
    if (!symlink_error) {
        rejects([&] { render_to_file(flame, opt, alias / "manifest.apocheckpoint", file); },
                "Output through a checkpoint symlink alias was admitted");
    }
    require(read(file.checkpoint_directory / "manifest.apocheckpoint") == safe_manifest,
            "Unsafe output path damaged checkpoint data");
    std::cout << "Checkpoints: committed pause/resume, identity, corruption, orphans, lock and "
                 "reconstruction\n";
}
void plan_tests(const fs::path &directory) {
    auto flame = fixture();
    auto opt = options(65539);
    auto file = file_options(0);
    file.disk_mib = 16384;
    flame.width = flame.height = 16384;
    flame.scale = 4096;
    opt.memory_mib = 16;
    const auto plan = plan_file_render(flame, opt, file);
    require(plan.width == 16384 && plan.height == 16384 && plan.host_bytes <= 16 * MiB &&
                plan.strip_rows > 0 &&
                plan.strip_count == (16384u + plan.strip_rows - 1) / plan.strip_rows,
            "16K file plan did not stay within its working budget");
    file.strip_rows = 16384;
    rejects([&] { plan_file_render(flame, opt, file); }, "An over-budget explicit strip was admitted");
    file.strip_rows = 0;
    flame.width = 16385;
    rejects([&] { plan_file_render(flame, opt, file); }, "A 16385-pixel output was admitted");
    flame = fixture();
    opt.memory_mib = 1;
    rejects([&] { plan_file_render(flame, opt, file); }, "Insufficient host budget was admitted");
    opt.memory_mib = 64;
    file.disk_mib = 1;
    rejects([&] { plan_file_render(flame, opt, file); }, "Insufficient disk budget was admitted");
    file.disk_mib = 64;
    opt.backend = static_cast<RenderBackend>(99);
    rejects([&] { plan_file_render(flame, opt, file); }, "Invalid backend was admitted");
    opt.backend = RenderBackend::cpu;
    opt.samples = UINT64_MAX;
    rejects([&] { plan_file_render(flame, opt, file); }, "Overflowing samples were admitted");
    opt = options();
    opt.memory_mib = UINT64_MAX;
    rejects([&] { plan_file_render(flame, opt, file); }, "Overflowing budget was admitted");
    opt = options();
    file.image.alpha = true;
    rejects([&] { plan_file_render(flame, opt, file); }, "PPM alpha was admitted");
    file.image.alpha = false;
    file.resume = true;
    rejects([&] { plan_file_render(flame, opt, file); }, "Implicit-directory resume was admitted");
    file.resume = false;
    flame.name.assign(2 * MiB, '&');
    rejects([&] { plan_file_render(flame, opt, file); }, "Unbudgeted expanded metadata was admitted");
    flame = fixture();
    opt.device.assign(65537, 'x');
    rejects([&] { plan_file_render(flame, opt, file); }, "Unbounded device identifier was admitted");

    flame = fixture();
    flame.name.assign(300000, '&');
    flame.width = flame.height = 1;
    flame.oversample = 1;
    opt = options(1);
    opt.memory_mib = 32;
    file = file_options(0);
    file.image.format = ImageFormat::png8;
    file.checkpoint_directory = directory / "expanded-metadata-checkpoint";
    file.disk_mib = 2;
    rejects([&] { plan_file_render(flame, opt, file); },
            "Disk budget omitted existing and staged expanded checkpoint manifests");
    file.disk_mib = 64;
    const auto metadata_plan = plan_file_render(flame, opt, file);
    const auto output = directory / "expanded-metadata.png";
    render_to_file(flame, opt, output, file);
    const auto manifest_size =
        fs::file_size(file.checkpoint_directory / "manifest.apocheckpoint");
    require(manifest_size > MiB, "Metadata regression did not exceed the old fixed allowance");
    std::uint64_t chunk_bytes = 0;
    for (const auto &entry : fs::directory_iterator(file.checkpoint_directory))
        if (entry.path().extension() == ".apohits")
            chunk_bytes += entry.file_size();
    require(metadata_plan.disk_bytes >=
                2 * manifest_size + chunk_bytes + fs::file_size(output),
            "Disk plan does not cover both manifest copies, chunks and output");
    std::cout << "Planner: 16K bounded plan, memory/disk/strip/overflow/model metadata rejections\n";
}
void precision_tests(const fs::path &directory) {
    auto flame = fixture();
    auto opt = options();
    auto file = file_options(3);
    const auto dense = render(flame, opt);
    file.image.format = ImageFormat::ppm16;
    const auto path = directory / "precision.ppm";
    render_to_file(flame, opt, path, file);
    const auto precise = ppm(path);
    require(precise.bits == 16 && std::any_of(precise.samples.begin(), precise.samples.end(),
                                              [](auto n) { return n % 257 != 0; }),
            "RGB16 was expanded from RGB8");
    for (std::size_t i = 0; i < precise.samples.size(); ++i)
        require(std::abs(double(precise.samples[i]) / 257 - dense.image.rgb[i]) <= 1,
                "RGB16 tone mapping is inconsistent with RGB8");

    const RGB color{.12345, .45678, .76543};
    flame.palette.fill(color);
    flame.vibrancy = 1;
    flame.brightness = .001;
    file.image = {ImageFormat::tiff16, true};
    const auto alpha_path = directory / "alpha.tiff";
    render_to_file(flame, opt, alpha_path, file);
    const auto alpha = tiff(alpha_path);
    require(alpha.channels == 4 && alpha.bits == 16, "Expected true RGBA16 output");
    const std::array palette{color.r, color.g, color.b};
    bool zero = false, partial = false, precision = false;
    for (std::size_t i = 0; i < alpha.samples.size(); i += 4) {
        const auto a = alpha.samples[i + 3];
        const bool empty =
            a == 0 && alpha.samples[i] == 0 && alpha.samples[i + 1] == 0 && alpha.samples[i + 2] == 0;
        zero |= empty;
        partial |= a > 0 && a < 65535;
        precision |= a % 257 != 0;
        for (unsigned c = 0; c < 3; ++c) {
            // A tiny positive coverage may round to zero at export while its
            // straight color remains defined; only truly empty samples are black.
            if (!empty)
                require(std::abs(double(alpha.samples[i + c]) - palette[c] * 65535) <= 2,
                        "RGBA export is not straight linear palette color");
        }
    }
    require(zero && partial && precision, "Alpha fixture lacks empty/partial/16-bit samples");
    flame.background = {.89, .72, .61};
    const auto changed_background = directory / "alpha-background.tiff";
    render_to_file(flame, opt, changed_background, file);
    require(tiff(changed_background).samples == alpha.samples,
            "Transparent output incorporated background");
    file.image = {ImageFormat::ppm16, false};
    render_to_file(flame, opt, directory / "composited.ppm", file);
    const auto opaque = ppm(directory / "composited.ppm");
    const std::array bg{flame.background.r, flame.background.g, flame.background.b};
    for (std::size_t i = 0; i < opaque.samples.size() / 3; ++i) {
        const auto a = double(alpha.samples[i * 4 + 3]) / 65535;
        for (unsigned c = 0; c < 3; ++c) {
            const auto expected = a * alpha.samples[i * 4 + c] + (1 - a) * bg[c] * 65535;
            require(std::abs(opaque.samples[i * 3 + c] - expected) <= 2,
                    "Opaque RGB16 and straight RGBA16 do not composite consistently");
        }
    }
    std::cout << "Precision: true RGB16 and straight RGBA16, coverage/background/compositing\n";
}
void fallback_checkpoint(const fs::path &directory) {
    const auto flame = fixture();
    auto opt = options(65539);
    const auto dense = render(flame, opt);
    opt.backend = RenderBackend::automatic;
    opt.device = "intentionally-unavailable-file-render-test-device";
    auto file = file_options();
    file.checkpoint_directory = directory / "fallback-checkpoint";
    const auto output = directory / "fallback.ppm";
    std::stop_source stopped;
    opt.stop_token = stopped.get_token();
    opt.progress = [&](auto completed, auto) {
        if (completed)
            stopped.request_stop();
    };
    rejects<RenderCancelled>([&] { render_to_file(flame, opt, output, file); },
                             "CPU fallback checkpoint did not pause");
    require(has_render_checkpoint(file.checkpoint_directory), "Fallback did not commit CPU checkpoint");
    opt.stop_token = {};
    opt.progress = {};
    file.resume = true;
    const auto resumed = render_to_file(flame, opt, output, file);
    require(resumed.backend == RenderBackend::cpu && !resumed.fallback_reason.empty() &&
                resumed.resumed,
            "Resumed fallback lost its actual backend or explanation");
    pixels_equal(ppm(output), dense, "Resumed CPU fallback changed the random stream");
    std::cout << "Fallback: unavailable requested device persisted CPU backend across partial resume\n";
}
void gpu_exact(const fs::path &directory, bool required, const std::string &selector) {
    const auto info = compute_devices();
    const auto found = std::find_if(info.devices.begin(), info.devices.end(), [&](const auto &device) {
        return device.usable &&
               (selector.empty() || selector == device.id || selector == std::to_string(device.index));
    });
    if (found == info.devices.end()) {
        require(!required && selector.empty(), "Required Vulkan device unavailable");
        std::cout << "Vulkan file comparison skipped: no usable device\n";
        return;
    }
    auto opt = options(8195);
    opt.backend = RenderBackend::vulkan;
    opt.device = found->id;
    opt.allow_cpu_fallback = false;
    for (const auto source : {"", "Apophysis 2.09", "Apophysis 7x"}) {
        const auto flame = fixture(source);
        const auto dense = render(flame, opt);
        for (auto rows : {1u, 9u}) {
            const auto path = directory / "gpu.ppm";
            const auto result = render_to_file(flame, opt, path, file_options(rows));
            require(result.backend == RenderBackend::vulkan && result.plotted == dense.plotted &&
                        result.invalid == dense.invalid,
                    "Vulkan file result used different sampling");
            pixels_equal(ppm(path), dense, "Vulkan streamed reconstruction differs from dense output");
        }
    }
    std::cout << "Vulkan: six exact RGB8 dense/strip comparisons on " << found->name << '\n';

    opt.samples = 65539;
    const auto flame = fixture();
    const auto dense = render(flame, opt);
    auto file = file_options();
    file.checkpoint_directory = directory / "gpu-checkpoint";
    const auto output = directory / "gpu-resumed.ppm";
    write(output, "existing GPU destination");
    std::stop_source stopped;
    opt.stop_token = stopped.get_token();
    opt.progress = [&](auto completed, auto) {
        if (completed)
            stopped.request_stop();
    };
    rejects<RenderCancelled>([&] { render_to_file(flame, opt, output, file); },
                             "Vulkan checkpoint did not pause");
    require(has_render_checkpoint(file.checkpoint_directory) &&
                read(output) == "existing GPU destination",
            "Vulkan pause did not preserve committed work and destination");
    opt.stop_token = {};
    opt.progress = {};
    file.resume = true;
    auto incompatible = file;
    incompatible.checkpoint_directory = directory / "gpu-incompatible";
    fs::copy(file.checkpoint_directory, incompatible.checkpoint_directory, fs::copy_options::recursive);
    const auto manifest = incompatible.checkpoint_directory / "manifest.apocheckpoint";
    auto bytes = read(manifest);
    constexpr std::string_view field = "spirv-fnv1a64=";
    const auto identity = bytes.find(field);
    require(identity != std::string::npos, "GPU checkpoint omitted shader identity");
    auto &digit = bytes.at(identity + field.size());
    digit = digit == '0' ? '1' : '0';
    const auto marker = bytes.rfind("\nHASH ");
    require(marker != std::string::npos, "GPU checkpoint omitted integrity footer");
    bytes.resize(marker + 1);
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char byte : bytes)
        hash = (hash ^ byte) * UINT64_C(1099511628211);
    std::ostringstream footer;
    footer << "HASH " << std::hex << hash << '\n';
    write(manifest, bytes + footer.str());
    require(has_render_checkpoint(incompatible.checkpoint_directory),
            "Identity mismatch fixture does not have a valid manifest checksum");
    const auto error = rejects([&] { render_to_file(flame, opt, output, incompatible); },
                               "Changed GPU/driver/shader identity was accepted");
    require(error.find("identity changed") != std::string::npos &&
                read(output) == "existing GPU destination",
            "GPU identity mismatch did not reject before publishing output");
    const auto resumed = render_to_file(flame, opt, output, file);
    require(resumed.backend == RenderBackend::vulkan && resumed.resumed,
            "Vulkan resume changed backend");
    pixels_equal(ppm(output), dense, "Partial Vulkan resume changed final pixels");
    std::cout << "Vulkan: partial checkpoint resumes exactly and rejects changed shader identity\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool require_vulkan = false;
        bool cpu_only = false;
        std::string selector;
        for (int i = 1; i < argc; ++i) {
            const auto option = std::string_view(argv[i]);
            if (option == "--require-vulkan")
                require_vulkan = true;
            else if (option == "--cpu-only")
                cpu_only = true;
            else if (option == "--device" && i + 1 < argc)
                selector = argv[++i];
            else
                throw std::invalid_argument(
                    "Use file-render-tests [--cpu-only | --require-vulkan] [--device ID|INDEX]");
        }
        if (cpu_only && (require_vulkan || !selector.empty()))
            throw std::invalid_argument("--cpu-only cannot select or require a Vulkan device");
        Temporary temporary;
        plan_tests(temporary.directory);
        cpu_exact(temporary.directory);
        checkpoint_tests(temporary.directory);
        precision_tests(temporary.directory);
        fallback_checkpoint(temporary.directory);
        if (!cpu_only)
            gpu_exact(temporary.directory, require_vulkan, selector);
        else
            std::cout << "Vulkan file execution not requested (--cpu-only)\n";
        std::cout << "File render tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "File render test failure: " << error.what() << '\n';
        return 1;
    }
}
