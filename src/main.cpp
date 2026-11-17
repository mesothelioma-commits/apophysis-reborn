#include "document_cli.hpp"
#include "legacy_document.hpp"
#include "render.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
namespace fs = std::filesystem;

void usage(std::ostream& out) {
    out << "Apophysis Reborn 0.5.0 | Creative tools and CPU renderer\n"
        << "  flame-cpu --flame input.flame --output image.bmp [options]\n"
        << "  flame-cpu inspect input.flame [--json]\n"
        << "  flame-cpu save input.flame --output copy.flame [--index N] [--name TEXT]\n"
        << "                 [--size W H] [--center X Y] [--scale PIXELS_PER_UNIT]\n"
        << "  flame-cpu export input.flame --output single.flame [--index N]\n"
        << "    save preserves the whole document. export writes one supported render model.\n"
        << "  flame-cpu --list-variations\n"
        << "  flame-cpu width height samples output.ppm    (foundation demo)\n"
        << "Options:\n"
        << "  --index N        flame in a collection (1-based, default 1)\n"
        << "  --samples N      iteration budget; default derives from XML quality and zoom\n"
        << "  --seed N         unsigned 64-bit seed (default 123456)\n"
        << "  --threads N      CPU workers (default 0 = automatic)\n"
        << "  --memory-mib N   histogram, output pixels and worker hit buffer budget (default 512)\n"
        << "Output formats: BMP (RGB24) and PPM (P6).\n";
}
template<class T>
T integer(std::string_view arg, std::string_view name) {
    T value{};
    auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
    if (ec != std::errc{} || end != arg.data() + arg.size() || arg.empty())
        throw std::invalid_argument("Invalid integer for " + std::string(name) + ": " + std::string(arg));
    return value;
}

int run(const std::vector<fs::path>& args) {
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        usage(std::cout);
        return 0;
    }
    if (args.size() == 1 && args[0] == "--list-variations") {
        for (const auto& v : apo::variation_registry()) std::cout << unsigned(v.id) << " " << v.name << "\n";
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
            if (i == args.size()) throw std::invalid_argument("Missing value for " + option);
            const auto& value = args[i++];
            if (option == "--flame") input = value;
            else if (option == "--output") output = value;
            else if (option == "--samples") samples = integer<std::uint64_t>(value.string(), option);
            else if (option == "--threads") options.threads = integer<unsigned>(value.string(), option);
            else if (option == "--seed") options.seed = integer<std::uint64_t>(value.string(), option);
            else if (option == "--index") index = integer<std::size_t>(value.string(), option);
            else if (option == "--memory-mib") options.memory_mib = integer<std::uint64_t>(value.string(), option);
            else throw std::invalid_argument("Unknown option " + option);
        }
        if (input.empty() || output.empty()) { usage(std::cerr); return 2; }
        std::string ext = output.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        if (ext != ".bmp" && ext != ".ppm") throw std::invalid_argument("Use .bmp or .ppm for output");
        if (fs::exists(output) && fs::equivalent(input, output))
            throw std::invalid_argument("Input and output refer to the same file");
        auto document = apo::LegacyDocument::load(input);
        if (index == 0 || index > document.entries().size())
            throw std::invalid_argument("--index must be between 1 and " + std::to_string(document.entries().size()));
        flame = document.render_model(index - 1);
        options.samples = samples ? *samples : apo::default_samples(flame);
    }
    auto ext = output.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    auto start = std::chrono::steady_clock::now();
    auto result = apo::render(flame, options);
    apo::write_image(output, result.image, ext == ".bmp");
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::cout << "Rendered " << result.image.width << "x" << result.image.height
              << " samples=" << options.samples << " plotted=" << result.plotted
              << " invalid_orbit_steps=" << result.invalid << " workers=" << result.workers
              << " seed=" << options.seed << " elapsed_ms=" << ms.count() << "\n";
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        std::vector<fs::path> arguments;
#ifdef _WIN32
        (void)argc; (void)argv;
        int count = 0;
        auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide) throw std::runtime_error("Cannot read Windows command line");
        struct FreeArgs { LPWSTR* args; ~FreeArgs(){ LocalFree(args); } } freed{wide};
        for (int i = 1; i < count; ++i) arguments.emplace_back(wide[i]);
#else
        for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
#endif
        return run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "flame-cpu: " << error.what() << "\n";
        return 1;
    }
}
