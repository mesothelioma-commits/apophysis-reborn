#include "document_cli.hpp"
#include "gpu.hpp"
#include "legacy_document.hpp"
#include "render.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace {
namespace fs = std::filesystem;

void usage(std::ostream &out) {
    out << "Apophysis Reborn 0.6.0 | CPU and Vulkan rendering\n"
        << "  flame-cpu --flame input.flame --output image.bmp [options]\n"
        << "  flame-cpu inspect input.flame [--json]\n"
        << "  flame-cpu save input.flame --output copy.flame [--index N] [--name TEXT]\n"
        << "                 [--size W H] [--center X Y] [--scale PIXELS_PER_UNIT]\n"
        << "  flame-cpu export input.flame --output single.flame [--index N]\n"
        << "    save preserves the whole document. export writes one supported render model.\n"
        << "  flame-cpu --list-variations\n"
        << "  flame-cpu --list-devices [--json]\n"
        << "  flame-cpu width height samples output.ppm    (foundation demo)\n"
        << "Options:\n"
        << "  --index N        flame in a collection (1-based, default 1)\n"
        << "  --samples N      iteration budget; default derives from XML quality and zoom\n"
        << "  --seed N         unsigned 64-bit seed (default 123456)\n"
        << "  --threads N      CPU workers (default 0 = automatic)\n"
        << "  --memory-mib N   histogram, output pixels and worker hit buffer budget (default 512)\n"
        << "  --backend MODE   cpu (default), auto, or vulkan\n"
        << "  --device ID      Vulkan discovery ID or zero-based device index\n"
        << "  --gpu-memory-mib N  Vulkan working-buffer budget (default 512)\n"
        << "  --no-fallback    report a Vulkan error instead of falling back to the CPU\n"
        << "Vulkan uses FP32 sampling and CPU image reconstruction. Automatic selection skips\n"
        << "software Vulkan devices unless --device explicitly selects one.\n"
        << "Output formats: BMP (RGB24) and PPM (P6).\n";
}
template <class T> T integer(std::string_view arg, std::string_view name) {
    T value{};
    auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    if (ec != std::errc{} || end != arg.data() + arg.size() || arg.empty())
        throw std::invalid_argument("Invalid integer for " + std::string(name) + ": " +
                                    std::string(arg));
    return value;
}

std::string quoted(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') {
            result += '\\';
            result += char(c);
        } else if (c < 0x20) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else
            result += char(c);
    }
    return result + '"';
}

void listDevices(bool json) {
    const auto report = apo::compute_devices();
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    if (json) {
        std::cout << "{\"compiled\":" << boolean(report.compiled)
                  << ",\"loader_available\":" << boolean(report.loader_available)
                  << ",\"diagnostic\":" << quoted(report.diagnostic) << ",\"devices\":[";
        bool first = true;
        for (const auto &device : report.devices) {
            if (!first)
                std::cout << ',';
            first = false;
            std::cout << "{\"index\":" << device.index << ",\"id\":" << quoted(device.id)
                      << ",\"name\":" << quoted(device.name) << ",\"type\":" << quoted(device.type)
                      << ",\"api_version\":" << quoted(device.api_version)
                      << ",\"driver_version\":" << quoted(device.driver_version)
                      << ",\"device_memory_mib\":" << device.device_memory_mib
                      << ",\"max_storage_buffer_bytes\":" << device.max_storage_buffer_bytes
                      << ",\"usable\":" << boolean(device.usable)
                      << ",\"software\":" << boolean(device.software)
                      << ",\"float64\":" << boolean(device.float64)
                      << ",\"int64\":" << boolean(device.int64)
                      << ",\"reason\":" << quoted(device.reason) << '}';
        }
        std::cout << "]}\n";
        return;
    }
    std::cout << "CPU reference renderer: available\nVulkan backend: "
              << (report.compiled ? "compiled" : "not compiled")
              << "; loader: " << (report.loader_available ? "available" : "unavailable") << '\n';
    if (!report.diagnostic.empty())
        std::cout << report.diagnostic << '\n';
    for (const auto &device : report.devices) {
        std::cout << '[' << device.index << "] " << device.name << " (" << device.type
                  << (device.software ? ", software" : "") << ")\n"
                  << "  ID: " << device.id << "\n  Vulkan " << device.api_version << "; driver "
                  << device.driver_version << "; device memory " << device.device_memory_mib
                  << " MiB; maximum storage buffer " << device.max_storage_buffer_bytes << " bytes\n"
                  << "  FP64: " << (device.float64 ? "yes" : "no")
                  << "; Int64: " << (device.int64 ? "yes" : "no")
                  << "; current FP32 kernel: " << (device.usable ? "available" : "unavailable") << '\n';
        if (!device.reason.empty())
            std::cout << "  " << device.reason << '\n';
    }
    if (report.devices.empty())
        std::cout << "No Vulkan compute devices were found.\n";
}

int run(const std::vector<fs::path> &args) {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        usage(std::cout);
        return 0;
    }
    if (!args.empty() && args[0] == "--list-devices") {
        if (args.size() > 2 || (args.size() == 2 && args[1] != "--json"))
            throw std::invalid_argument("Use --list-devices or --list-devices --json");
        listDevices(args.size() == 2);
        return 0;
    }
    if (args.size() == 1 && args[0] == "--list-variations") {
        for (const auto &v : apo::variation_registry())
            std::cout << unsigned(v.id) << " " << v.name << "\n";
        return 0;
    }
    if (!args.empty() && (args[0] == "inspect" || args[0] == "save" || args[0] == "export"))
        return apo::document_command(args);
    apo::Flame flame;
    apo::RenderOptions options;
    fs::path output;
    const bool legacy = args.size() == 4 && !args[0].string().starts_with("-");
    if (legacy) {
        flame = apo::demo_flame();
        const int width = integer<int>(args[0].string(), "width");
        const int height = integer<int>(args[1].string(), "height");
        flame.scale *= double(std::min(width, height)) / std::min(flame.width, flame.height);
        flame.width = width;
        flame.height = height;
        options.samples = integer<std::uint64_t>(args[2].string(), "samples");
        output = args[3];
    } else {
        fs::path input;
        std::size_t index = 1;
        std::optional<std::uint64_t> samples;
        for (std::size_t i = 0; i < args.size();) {
            const auto option = args[i++].string();
            if (option == "--no-fallback") {
                options.allow_cpu_fallback = false;
                continue;
            }
            if (i == args.size())
                throw std::invalid_argument("Missing value for " + option);
            const auto &value = args[i++];
            if (option == "--flame")
                input = value;
            else if (option == "--output")
                output = value;
            else if (option == "--samples")
                samples = integer<std::uint64_t>(value.string(), option);
            else if (option == "--threads")
                options.threads = integer<unsigned>(value.string(), option);
            else if (option == "--seed")
                options.seed = integer<std::uint64_t>(value.string(), option);
            else if (option == "--index")
                index = integer<std::size_t>(value.string(), option);
            else if (option == "--memory-mib")
                options.memory_mib = integer<std::uint64_t>(value.string(), option);
            else if (option == "--gpu-memory-mib")
                options.gpu_memory_mib = integer<std::uint64_t>(value.string(), option);
            else if (option == "--device")
                options.device = value.string();
            else if (option == "--backend") {
                if (value == "cpu")
                    options.backend = apo::RenderBackend::cpu;
                else if (value == "auto")
                    options.backend = apo::RenderBackend::automatic;
                else if (value == "vulkan")
                    options.backend = apo::RenderBackend::vulkan;
                else
                    throw std::invalid_argument("--backend must be cpu, auto or vulkan");
            } else
                throw std::invalid_argument("Unknown option " + option);
        }
        if (input.empty() || output.empty()) {
            usage(std::cerr);
            return 2;
        }
        std::string ext = output.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".bmp" && ext != ".ppm")
            throw std::invalid_argument("Use .bmp or .ppm for output");
        if (fs::exists(output) && fs::equivalent(input, output))
            throw std::invalid_argument("Input and output refer to the same file");
        auto document = apo::LegacyDocument::load(input);
        if (index == 0 || index > document.entries().size())
            throw std::invalid_argument("--index must be between 1 and " +
                                        std::to_string(document.entries().size()));
        flame = document.render_model(index - 1);
        options.samples = samples ? *samples : apo::default_samples(flame);
    }
    auto ext = output.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    auto start = std::chrono::steady_clock::now();
    auto result = apo::render(flame, options);
    apo::write_image(output, result.image, ext == ".bmp");
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << "Rendered " << result.image.width << "x" << result.image.height
              << " samples=" << options.samples << " plotted=" << result.plotted
              << " invalid_orbit_steps=" << result.invalid << " workers=" << result.workers
              << " seed=" << options.seed << " elapsed_ms=" << ms.count()
              << " backend=" << apo::backend_name(result.backend)
              << " device=" << quoted(result.device_name) << " sampling_ms=" << result.sampling_ms
              << " reconstruction_ms=" << result.reconstruction_ms << "\n";
    if (!result.fallback_reason.empty())
        std::cout << "CPU fallback: " << result.fallback_reason << '\n';
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::vector<fs::path> arguments;
#ifdef _WIN32
        (void)argc;
        (void)argv;
        int count = 0;
        auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide)
            throw std::runtime_error("Cannot read Windows command line");
        struct FreeArgs {
            LPWSTR *args;
            ~FreeArgs() {
                LocalFree(args);
            }
        } freed{wide};
        for (int i = 1; i < count; ++i)
            arguments.emplace_back(wide[i]);
#else
        for (int i = 1; i < argc; ++i)
            arguments.emplace_back(argv[i]);
#endif
        return run(arguments);
    } catch (const std::exception &error) {
        std::cerr << "flame-cpu: " << error.what() << "\n";
        return 1;
    }
}
