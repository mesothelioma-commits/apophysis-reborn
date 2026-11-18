#include "document_cli.hpp"
#include "extension_cli.hpp"
#include "animation_cli.hpp"
#include "build_info.hpp"
#include "gpu.hpp"
#include "legacy_document.hpp"
#include "plugin.hpp"
#include "project_document.hpp"
#include "render.hpp"
#include "render_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <optional>
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
  out << "Apophysis Reborn " << apo::application_version() << " | Flame renderer\n"
      << "  flame-cpu --flame input.flame --output image.png [options]\n"
      << "  flame-cpu inspect input.flame [--json]\n"
      << "  flame-cpu save input.flame --output copy.flame [--index N] [--name "
         "TEXT]\n"
      << "                 [--size W H] [--center X Y] [--scale "
         "PIXELS_PER_UNIT]\n"
      << "  flame-cpu export input.flame --output single.flame [--index N]\n"
      << "    save preserves the whole document. export writes one supported "
         "render model.\n"
      << "  flame-cpu --list-variations\n"
      << "  flame-cpu --list-devices [--json]\n"
      << "  flame-cpu --version\n"
      << "  flame-cpu --build-info [--json]\n"
      << "  flame-cpu width height samples output.ppm    (foundation demo)\n"
      << "Options:\n"
      << "  --index N        flame in a collection (1-based; project selection "
         "or 1)\n"
      << "  --samples N      iteration budget; default derives from XML "
         "quality and zoom\n"
      << "  --seed N         unsigned 64-bit seed (default 123456)\n"
      << "  --threads N      CPU workers (default 0 = automatic)\n"
      << "  --size W H       output dimensions, up to 16384 each; fits the "
         "existing camera\n"
      << "  --oversample N   histogram oversampling factor\n"
      << "  --filter-radius N  Gaussian reconstruction radius\n"
      << "  --memory-mib N   sampling/reconstruction working-buffer budget "
         "(default 512)\n"
      << "  --backend MODE   cpu (default), auto, or vulkan\n"
      << "  --device ID      Vulkan discovery ID or zero-based device index\n"
      << "  --gpu-memory-mib N  Vulkan working-buffer budget (default 512)\n"
      << "  --no-fallback    report a Vulkan error instead of falling back to "
         "the CPU\n"
      << "  --format NAME    bmp, ppm, png, tiff; or ppm16/png16/tiff16 "
         "(otherwise extension)\n"
      << "  --bit-depth N    8 (default) or 16; BMP supports 8-bit channels "
         "only\n"
      << "  --alpha          straight RGBA output; requires PNG or TIFF\n"
      << "  --disk-mib N     conservative work/output disk budget (default "
         "16384)\n"
      << "  --strip-rows N   reconstruction strip height (default chooses "
         "within memory)\n"
      << "  --checkpoint DIR  retain committed sampling chunks for "
         "restart-safe resume\n"
      << "  --resume         resume --checkpoint using the same "
         "flame/sampling/export settings\n"
      << "  --pause-after N  stop after at least N samples are checkpointed; "
         "exits 75\n"
      << "  --estimate       print the memory/disk/strip plan without "
         "rendering or writing "
         "files\n"
      << "Vulkan uses FP32 sampling and CPU image reconstruction. Automatic "
         "selection skips\n"
      << "software Vulkan devices unless --device explicitly selects one.\n"
      << "Sampling is global; disk-backed strip reconstruction retains filter "
         "overlap.\n"
      << "Output formats: BMP RGB8, PPM RGB8/16, PNG and TIFF RGB/RGBA8/16.\n"
      << "Ctrl+C cancels safely; an explicit checkpoint retains completed "
         "sampling chunks.\n";
  apo::extension_usage(out);
  apo::animation_usage(out);
}
template <class T> T integer(std::string_view arg, std::string_view name) {
  T value{};
  auto [end, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), value);
  if (ec != std::errc{} || end != arg.data() + arg.size() || arg.empty())
    throw std::invalid_argument("Invalid integer for " + std::string(name) +
                                ": " + std::string(arg));
  return value;
}

double real(std::string_view arg, std::string_view name) {
  double value{};
  const auto [end, ec] =
      std::from_chars(arg.data(), arg.data() + arg.size(), value);
  if (ec != std::errc{} || end != arg.data() + arg.size() || arg.empty() ||
      !std::isfinite(value))
    throw std::invalid_argument("Invalid finite number for " +
                                std::string(name));
  return value;
}

volatile std::sig_atomic_t render_signal = 0;
extern "C" void interrupt_render(int signal) { render_signal = signal; }
struct RenderSignals {
  using Handler = void (*)(int);
  Handler previous_interrupt, previous_terminate;
  RenderSignals() {
    render_signal = 0;
    previous_interrupt = std::signal(SIGINT, interrupt_render);
    if (previous_interrupt == SIG_ERR)
      throw std::runtime_error("Cannot install render interruption handler");
    previous_terminate = std::signal(SIGTERM, interrupt_render);
    if (previous_terminate == SIG_ERR) {
      std::signal(SIGINT, previous_interrupt);
      throw std::runtime_error("Cannot install render termination handler");
    }
  }
  ~RenderSignals() {
    std::signal(SIGINT, previous_interrupt);
    std::signal(SIGTERM, previous_terminate);
  }
};

std::string jsonQuoted(std::string_view value) {
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
              << ",\"diagnostic\":" << jsonQuoted(report.diagnostic)
              << ",\"devices\":[";
    bool first = true;
    for (const auto &device : report.devices) {
      if (!first)
        std::cout << ',';
      first = false;
      std::cout << "{\"index\":" << device.index
                << ",\"id\":" << jsonQuoted(device.id)
                << ",\"name\":" << jsonQuoted(device.name)
                << ",\"type\":" << jsonQuoted(device.type)
                << ",\"api_version\":" << jsonQuoted(device.api_version)
                << ",\"driver_version\":" << jsonQuoted(device.driver_version)
                << ",\"device_memory_mib\":" << device.device_memory_mib
                << ",\"max_storage_buffer_bytes\":"
                << device.max_storage_buffer_bytes
                << ",\"usable\":" << boolean(device.usable)
                << ",\"software\":" << boolean(device.software)
                << ",\"float64\":" << boolean(device.float64)
                << ",\"int64\":" << boolean(device.int64)
                << ",\"reason\":" << jsonQuoted(device.reason) << '}';
    }
    std::cout << "]}\n";
    return;
  }
  std::cout << "CPU reference renderer: available\nVulkan backend: "
            << (report.compiled ? "compiled" : "not compiled") << "; loader: "
            << (report.loader_available ? "available" : "unavailable") << '\n';
  if (!report.diagnostic.empty())
    std::cout << report.diagnostic << '\n';
  for (const auto &device : report.devices) {
    std::cout << '[' << device.index << "] " << device.name << " ("
              << device.type << (device.software ? ", software" : "") << ")\n"
              << "  ID: " << device.id << "\n  Vulkan " << device.api_version
              << "; driver " << device.driver_version << "; device memory "
              << device.device_memory_mib << " MiB; maximum storage buffer "
              << device.max_storage_buffer_bytes << " bytes\n"
              << "  FP64: " << (device.float64 ? "yes" : "no")
              << "; Int64: " << (device.int64 ? "yes" : "no")
              << "; current FP32 kernel: "
              << (device.usable ? "available" : "unavailable") << '\n';
    if (!device.reason.empty())
      std::cout << "  " << device.reason << '\n';
  }
  if (report.devices.empty())
    std::cout << "No Vulkan compute devices were found.\n";
}

int run(const std::vector<fs::path> &args) {
  apo::gpu::ContextScope contextScope;
  if (!args.empty() && args.front() == "--version") {
    if (args.size() != 1)
      throw std::invalid_argument("Use --version without rendering or plugin arguments");
    std::cout << "Apophysis Reborn " << apo::application_version() << '\n';
    return 0;
  }
  if (!args.empty() && args.front() == "--build-info") {
    if (args.size() > 2 || (args.size() == 2 && args[1] != "--json"))
      throw std::invalid_argument("Use --build-info or --build-info --json");
    std::cout << (args.size() == 2 ? apo::build_info_json() + '\n'
                                 : apo::build_info_text());
    return 0;
  }
  if (!args.empty() && args.front() == "--plugin") {
    std::size_t used = 0;
    while (used < args.size() && args[used] == "--plugin") {
      if (++used == args.size())
        throw std::invalid_argument("--plugin needs a trusted manifest path");
      const auto info = apo::enable_plugin(args[used++]);
      std::cerr << "Enabled trusted native plugin " << info.name << " ("
                << info.fingerprint << ")\n";
    }
    return run({args.begin() + used, args.end()});
  }
  if (const auto extension = apo::extension_command(args))
    return *extension;
  if (const auto animation = apo::animation_command(args))
    return *animation;
  if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
    usage(std::cout);
    return 0;
  }
  if (!args.empty() && args[0] == "--list-devices") {
    if (args.size() > 2 || (args.size() == 2 && args[1] != "--json"))
      throw std::invalid_argument(
          "Use --list-devices or --list-devices --json");
    listDevices(args.size() == 2);
    return 0;
  }
  if (args.size() == 1 && args[0] == "--list-variations") {
    for (const auto &v : apo::variation_registry())
      std::cout << unsigned(v.id) << " " << v.name << "\n";
    return 0;
  }
  if (!args.empty() &&
      (args[0] == "inspect" || args[0] == "save" || args[0] == "export"))
    return apo::document_command(args);
  apo::Flame flame;
  apo::RenderOptions options;
  apo::FileRenderOptions file;
  fs::path input, output;
  std::optional<unsigned> bit_depth;
  std::optional<std::string> format;
  std::optional<std::uint64_t> pause_after;
  bool estimate = false;
  const bool legacy = args.size() == 4 && !args[0].string().starts_with("-");
  if (legacy) {
    flame = apo::demo_flame();
    const int width = integer<int>(args[0].string(), "width");
    const int height = integer<int>(args[1].string(), "height");
    flame.scale *=
        double(std::min(width, height)) / std::min(flame.width, flame.height);
    flame.width = width;
    flame.height = height;
    options.samples = integer<std::uint64_t>(args[2].string(), "samples");
    output = args[3];
  } else {
    std::size_t index = 1;
    bool index_set = false;
    std::optional<std::uint64_t> samples;
    std::optional<std::array<int, 2>> dimensions;
    std::optional<int> oversample;
    std::optional<double> filter_radius;
    for (std::size_t i = 0; i < args.size();) {
      const auto option = args[i++].string();
      if (option == "--no-fallback") {
        options.allow_cpu_fallback = false;
        continue;
      }
      if (option == "--alpha") {
        file.image.alpha = true;
        continue;
      }
      if (option == "--resume") {
        file.resume = true;
        continue;
      }
      if (option == "--estimate") {
        estimate = true;
        continue;
      }
      if (option == "--size") {
        if (args.size() - i < 2)
          throw std::invalid_argument("--size requires width and height");
        const auto width = integer<int>(args[i++].string(), "width");
        const auto height = integer<int>(args[i++].string(), "height");
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
          throw std::invalid_argument(
              "Output dimensions must be between 1 and 16384");
        dimensions = std::array{width, height};
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
      else if (option == "--index") {
        index = integer<std::size_t>(value.string(), option);
        index_set = true;
      } else if (option == "--memory-mib")
        options.memory_mib = integer<std::uint64_t>(value.string(), option);
      else if (option == "--gpu-memory-mib")
        options.gpu_memory_mib = integer<std::uint64_t>(value.string(), option);
      else if (option == "--disk-mib")
        file.disk_mib = integer<std::uint64_t>(value.string(), option);
      else if (option == "--strip-rows") {
        file.strip_rows = integer<unsigned>(value.string(), option);
        if (!file.strip_rows)
          throw std::invalid_argument(
              "--strip-rows must be positive; omit it for automatic selection");
      } else if (option == "--checkpoint")
        file.checkpoint_directory = value;
      else if (option == "--pause-after")
        pause_after = integer<std::uint64_t>(value.string(), option);
      else if (option == "--format")
        format = value.string();
      else if (option == "--bit-depth")
        bit_depth = integer<unsigned>(value.string(), option);
      else if (option == "--oversample")
        oversample = integer<int>(value.string(), option);
      else if (option == "--filter-radius")
        filter_radius = real(value.string(), option);
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
    if (input.empty() || (output.empty() && !estimate)) {
      usage(std::cerr);
      return 2;
    }
    if (!output.empty() && fs::exists(output) && fs::equivalent(input, output))
      throw std::invalid_argument("Input and output refer to the same file");
    auto document_file = apo::load_document_file(input);
    if (!index_set && document_file.project)
      index = document_file.project->selected_index + 1;
    auto document = std::move(document_file.document);
    if (index == 0 || index > document.entries().size())
      throw std::invalid_argument("--index must be between 1 and " +
                                  std::to_string(document.entries().size()));
    flame = document.render_model(index - 1);
    if (dimensions) {
      flame.scale *= std::min(double((*dimensions)[0]) / flame.width,
                              double((*dimensions)[1]) / flame.height);
      flame.width = (*dimensions)[0];
      flame.height = (*dimensions)[1];
    }
    if (oversample)
      flame.oversample = *oversample;
    if (filter_radius)
      flame.filter_radius = *filter_radius;
    options.samples = samples ? *samples : apo::default_samples(flame);
  }
  if (format) {
    const auto named = apo::parse_image_format(*format);
    const bool named_depth = format->ends_with("8") || format->ends_with("16");
    if (named_depth && bit_depth && *bit_depth != apo::image_format_bits(named))
      throw std::invalid_argument(
          "--format and --bit-depth select different sample depths");
    const auto extension = std::string(apo::image_format_extension(named));
    file.image.format = apo::image_format_for_path(
        "render" + extension,
        bit_depth.value_or(apo::image_format_bits(named)));
    if (!output.empty() &&
        apo::image_format_for_path(output,
                                   apo::image_format_bits(file.image.format)) !=
            file.image.format)
      throw std::invalid_argument("Output extension does not match --format");
  } else {
    file.image.format = apo::image_format_for_path(
        output.empty() ? fs::path("estimate.png") : output,
        bit_depth.value_or(8));
  }
  if (pause_after && (!*pause_after || *pause_after > options.samples ||
                      file.checkpoint_directory.empty()))
    throw std::invalid_argument("--pause-after must be between 1 and --samples "
                                "and requires --checkpoint");
  const auto plan = apo::plan_file_render(flame, options, file);
  if (estimate) {
    std::cout << "Render plan: width=" << plan.width
              << " height=" << plan.height << " samples=" << options.samples
              << " format=" << apo::image_format_name(file.image.format)
              << " alpha=" << file.image.alpha
              << " requested_backend=" << apo::backend_name(options.backend)
              << " host_bytes=" << plan.host_bytes
              << " disk_bytes=" << plan.disk_bytes
              << " strip_rows=" << plan.strip_rows
              << " strips=" << plan.strip_count << '\n'
              << "Conservative working-buffer and disk estimates; no render or "
                 "output was written.\n"
              << "Render speed and ETA require measured progress on the "
                 "selected machine.\n";
    return 0;
  }
  RenderSignals signals;
  std::stop_source stop;
  options.stop_token = stop.get_token();
  bool requested_pause = false;
  std::uint64_t completed = 0, phase_samples = 0;
  double phase_started = 0;
  std::string last_phase;
  unsigned last_percent = 101;
  options.progress = [&](std::uint64_t, std::uint64_t) {
    if (render_signal)
      stop.request_stop();
  };
  file.status = [&](const apo::FileRenderProgress &progress) {
    completed = progress.completed_samples;
    if (render_signal)
      stop.request_stop();
    if (pause_after && progress.phase == "Sampling" &&
        completed >= *pause_after) {
      requested_pause = true;
      stop.request_stop();
    }
    const bool sampling = progress.phase == "Sampling";
    if (progress.phase != last_phase) {
      phase_started = progress.elapsed_seconds;
      phase_samples = completed;
      last_percent = 101;
    }
    const auto percent =
        sampling ? unsigned(completed * 100 / progress.total_samples)
        : progress.total_strips
            ? unsigned(std::uint64_t(progress.completed_strips) * 100 /
                       progress.total_strips)
            : 0;
    if (progress.phase != last_phase || percent != last_percent) {
      std::cerr << progress.phase << " " << percent << "% samples=" << completed
                << '/' << progress.total_samples;
      const auto elapsed = progress.elapsed_seconds - phase_started;
      if (sampling && elapsed >= 1 && completed > phase_samples &&
          completed < progress.total_samples)
        std::cerr << " eta_seconds=" << std::fixed << std::setprecision(1)
                  << elapsed * double(progress.total_samples - completed) /
                         double(completed - phase_samples);
      else if (!sampling && elapsed >= 1 && progress.completed_strips &&
               progress.completed_strips < progress.total_strips)
        std::cerr << " eta_seconds=" << std::fixed << std::setprecision(1)
                  << elapsed *
                         (progress.total_strips - progress.completed_strips) /
                         progress.completed_strips;
      std::cerr << '\n';
      last_phase = progress.phase;
      last_percent = percent;
    }
  };
  const auto start = std::chrono::steady_clock::now();
  apo::FileRenderResult result;
  try {
    result = apo::render_to_file(flame, options, output, file);
  } catch (const apo::RenderCancelled &) {
    const bool saved = !file.checkpoint_directory.empty() &&
                       apo::has_render_checkpoint(file.checkpoint_directory);
    if (requested_pause) {
      if (!saved)
        throw std::runtime_error(
            "Pause did not produce a valid render checkpoint");
      std::cout << "Paused at a committed checkpoint: completed_samples="
                << completed << " total_samples=" << options.samples << '\n';
      return 75;
    }
    std::cerr
        << "Rendering interrupted; the destination was not replaced."
        << (saved ? " Committed checkpoint chunks are available for --resume.\n"
                  : "\n");
    return render_signal == SIGTERM ? 143 : 130;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  std::cout << "Rendered " << flame.width << "x" << flame.height
            << " samples=" << options.samples << " plotted=" << result.plotted
            << " invalid_orbit_steps=" << result.invalid
            << " workers=" << result.workers << " seed=" << options.seed
            << " elapsed_ms=" << ms.count()
            << " backend=" << apo::backend_name(result.backend)
            << " device=" << jsonQuoted(result.device_name)
            << " sampling_ms=" << result.sampling_ms
            << " reconstruction_ms=" << result.reconstruction_ms
            << " format=" << apo::image_format_name(file.image.format)
            << " alpha=" << file.image.alpha << " resumed=" << result.resumed
            << "\n";
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
      ~FreeArgs() { LocalFree(args); }
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
