#include "animation_cli.hpp"
#include "animation.hpp"
#include "animation_export.hpp"
#include "flame_io.hpp"
#include "output_file.hpp"
#include "plugin.hpp"
#include "project_document.hpp"
#include "script_host.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace apo {
namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path &path) {
  const auto s = path.u8string();
  return {s.begin(), s.end()};
}
template <class T> T number(std::string_view value) {
  T n{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
  if (value.empty() || error != std::errc{} || end != value.data() + value.size())
    throw std::invalid_argument("Invalid integer: " + std::string(value));
  return n;
}
std::string json(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\')
      out << '\\' << char(c);
    else if (c < 32)
      out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c)
          << std::dec;
    else
      out << char(c);
  }
  out << '"';
  return out.str();
}
struct Arguments {
  std::map<std::string, fs::path> values;
  bool has(const std::string &key) const { return values.contains(key); }
  fs::path path(const std::string &key, fs::path fallback = {}) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  }
  std::string text(const std::string &key, std::string fallback = {}) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : utf8(it->second);
  }
  template <class T> T integer(const std::string &key, T fallback) const {
    return has(key) ? number<T>(text(key)) : fallback;
  }
};
Arguments arguments(const std::vector<fs::path> &args,
                    std::initializer_list<std::string_view> allowed) {
  const std::set<std::string_view> flags{
      "--overwrite", "--replace-animation", "--replace-keyframe", "--json",
      "--resume", "--estimate", "--alpha", "--no-cpu-fallback"};
  Arguments result;
  for (std::size_t i = 3; i < args.size();) {
    const auto key = utf8(args[i++]);
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      throw std::invalid_argument("Unknown or inapplicable animation option " + key);
    if (result.has(key))
      throw std::invalid_argument("Duplicate animation option " + key);
    if (flags.contains(key))
      result.values.emplace(key, fs::path{});
    else {
      if (i == args.size())
        throw std::invalid_argument("Missing value for " + key);
      result.values.emplace(key, args[i++]);
    }
  }
  return result;
}
template <class Enum>
Enum choice(std::string_view value,
            std::initializer_list<std::pair<std::string_view, Enum>> choices) {
  for (const auto &[name, option] : choices)
    if (value == name)
      return option;
  throw std::invalid_argument("Unsupported animation option value: " + std::string(value));
}
AnimationRate rate(std::string_view text) {
  const auto slash = text.find('/');
  if (slash == std::string_view::npos)
    return {number<std::uint32_t>(text), 1};
  return {number<std::uint32_t>(text.substr(0, slash)),
          number<std::uint32_t>(text.substr(slash + 1))};
}
AnimationEasing easing(std::string_view text) {
  return choice<AnimationEasing>(text, {{"hold", AnimationEasing::hold},
                                      {"linear", AnimationEasing::linear},
                                      {"smoothstep", AnimationEasing::smoothstep},
                                      {"smootherstep", AnimationEasing::smootherstep}});
}
AnimationTopology topology(std::string_view text) {
  return choice<AnimationTopology>(text, {{"strict", AnimationTopology::strict},
                                         {"fade-unmatched", AnimationTopology::fade_unmatched}});
}
std::vector<std::string> ids(std::string_view text) {
  if (text.empty())
    return {};
  std::vector<std::string> result;
  while (true) {
    const auto comma = text.find(',');
    const auto id = text.substr(0, comma);
    if (id.empty())
      throw std::invalid_argument("Transform ID lists cannot contain empty entries");
    result.emplace_back(id);
    if (comma == std::string_view::npos)
      break;
    text.remove_prefix(comma + 1);
  }
  return result;
}
void settings(AnimationClip &clip, const Arguments &args) {
  if (args.has("--name"))
    clip.name = args.text("--name");
  if (args.has("--fps"))
    clip.rate = rate(args.text("--fps"));
  if (args.has("--frames"))
    clip.frame_count = args.integer<std::uint32_t>("--frames", 0);
  if (args.has("--seed"))
    clip.seed = args.integer<std::uint64_t>("--seed", 0);
  if (args.has("--seed-mode"))
    clip.seed_mode = choice<AnimationSeedMode>(args.text("--seed-mode"),
        {{"fixed", AnimationSeedMode::fixed}, {"per-frame", AnimationSeedMode::per_frame}});
  if (args.has("--loop"))
    clip.loop = choice<AnimationLoop>(args.text("--loop"),
        {{"once", AnimationLoop::once}, {"repeat", AnimationLoop::repeat},
         {"ping-pong", AnimationLoop::ping_pong}});
  if (args.has("--affine"))
    clip.affine = choice<AnimationAffine>(args.text("--affine"),
        {{"linear", AnimationAffine::linear}, {"polar", AnimationAffine::polar}});
  if (args.has("--palette"))
    clip.palette = choice<AnimationPalette>(args.text("--palette"),
        {{"rgb", AnimationPalette::rgb}, {"hsv-shortest", AnimationPalette::hsv_shortest},
         {"linear-rgb", AnimationPalette::linear_rgb}, {"hsl-shortest", AnimationPalette::hsl_shortest}});
  if (args.has("--angle"))
    clip.camera_angle = choice<AnimationAngle>(args.text("--angle"),
        {{"shortest", AnimationAngle::shortest}, {"unwrapped", AnimationAngle::unwrapped}});
  if (args.has("--topology"))
    clip.topology = topology(args.text("--topology"));
  if (args.has("--easing"))
    for (auto &keyframe : clip.keyframes)
      keyframe.easing = easing(args.text("--easing"));
}
std::size_t selected(const DocumentFile &input, const Arguments &args,
                      const std::string &option = "--index") {
  const auto index = args.integer<std::size_t>(
      option, input.project ? input.project->selected_index + 1 : 1);
  if (!index || index > input.document.entries().size())
    throw std::invalid_argument("Flame index is out of range (indices start at 1)");
  return index - 1;
}
std::vector<fs::path> protected_files(std::initializer_list<fs::path> files) {
  std::vector<fs::path> result(files);
  for (const auto &plugin : enabled_plugins()) {
    result.push_back(plugin.manifest_path);
    result.push_back(plugin.library_path);
  }
  return result;
}
fs::path output_path(const fs::path &path, bool overwrite,
                     const std::vector<fs::path> &protectedFiles) {
  if (path.empty() || path.filename().empty())
    throw std::invalid_argument("Specify --output PATH");
  const auto parent = path.has_parent_path() ? path.parent_path() : fs::current_path();
  if (fs::is_symlink(fs::symlink_status(path)))
    throw std::invalid_argument("Animation output cannot replace a symlink");
  ScriptResult result;
  ScriptRequest request;
  request.kind = ScriptRequestKind::export_flame;
  request.bytes = "animation output";
  request.destination = path.filename();
  result.requests.push_back(std::move(request));
  return plan_script_outputs(result, parent, overwrite, protectedFiles).front();
}
void write(const fs::path &path, std::string_view bytes) {
  write_atomic(path, [&](std::ostream &out) {
    out.write(bytes.data(), std::streamsize(bytes.size()));
  });
}
void replacement_allowed(const DocumentFile &input, const Arguments &args) {
  if (input.project && input.project->animation_data && !args.has("--replace-animation"))
    throw std::invalid_argument(
        "The source project already contains animation data; use --replace-animation to explicitly replace it in the new project");
}
AnimationClip project_clip(const DocumentFile &input) {
  if (!input.project || !input.project->animation_data)
    throw std::invalid_argument("Choose a native .apoproject containing an animation");
  return parse_animation(*input.project->animation_data);
}
void save_clip(const DocumentFile &input, AnimationClip clip,
               std::size_t selected_index, const Arguments &args,
               const std::vector<fs::path> &protectedFiles) {
  const auto requested = args.path("--output");
  if (!is_project_path(requested))
    throw std::invalid_argument("Animations are saved as native .apoproject files");
  // Preparation detects missing native modules or unsupported correspondence
  // before publication. Interpolated models are validated by frame preflight.
  PreparedAnimation prepared(clip);
  auto project = input.project.value_or(ProjectFile{});
  project.library_bytes = input.document.bytes();
  project.selected_index = selected_index;
  project.animation_data = serialize_animation(clip);
  const auto bytes = serialize_project(project);
  const auto output = output_path(requested, args.has("--overwrite"), protectedFiles);
  write(output, bytes);
  std::cout << "Saved animation " << std::quoted(clip.name) << ": "
            << clip.frame_count << " frames at " << clip.rate.numerator << '/'
            << clip.rate.denominator << " fps, " << clip.keyframes.size()
            << " keyframes; " << utf8(output) << '\n';
  for (const auto &diagnostic : prepared.diagnostics())
    std::cerr << "Animation: " << diagnostic << '\n';
}
int create(const std::vector<fs::path> &args, bool preset_command) {
  const auto options = arguments(args, {"--output", "--index", "--preset", "--frames", "--fps",
      "--name", "--seed", "--seed-mode", "--loop", "--affine", "--palette", "--angle",
      "--easing", "--topology", "--overwrite", "--replace-animation"});
  if (preset_command && !options.has("--preset"))
    throw std::invalid_argument("animation preset requires --preset NAME");
  auto input = load_document_file(args[2]);
  replacement_allowed(input, options);
  const auto index = selected(input, options);
  const auto preset = choice<AnimationPreset>(options.text("--preset", "still"),
      {{"still", AnimationPreset::still}, {"camera-orbit", AnimationPreset::camera_orbit},
       {"zoom", AnimationPreset::zoom}, {"palette-cycle", AnimationPreset::palette_cycle}});
  auto clip = make_animation_preset(input.document, index, preset,
      options.integer<std::uint32_t>("--frames", 121),
      rate(options.text("--fps", "30")), options.integer<std::uint64_t>("--seed", 123456));
  settings(clip, options);
  save_clip(input, std::move(clip), index, options, protected_files({args[2]}));
  return 0;
}
int morph(const std::vector<fs::path> &args) {
  const auto options = arguments(args, {"--to", "--output", "--from-index", "--to-index",
      "--from-ids", "--to-ids", "--frames", "--fps", "--name", "--seed", "--seed-mode",
      "--loop", "--affine", "--palette", "--angle", "--easing", "--topology", "--overwrite",
      "--replace-animation"});
  if (!options.has("--to"))
    throw std::invalid_argument("animation morph requires --to FLAME_OR_PROJECT");
  auto from = load_document_file(args[2]);
  auto to = load_document_file(options.path("--to"));
  replacement_allowed(from, options);
  const auto index = selected(from, options, "--from-index");
  auto clip = make_morph_animation(from.document, index, to.document,
      selected(to, options, "--to-index"), options.integer<std::uint32_t>("--frames", 121),
      rate(options.text("--fps", "30")), ids(options.text("--from-ids")),
      ids(options.text("--to-ids")), topology(options.text("--topology", "strict")),
      options.integer<std::uint64_t>("--seed", 123456));
  settings(clip, options);
  save_clip(from, std::move(clip), index, options,
            protected_files({args[2], options.path("--to")}));
  return 0;
}
int import_timed(const std::vector<fs::path> &args) {
  const auto options = arguments(args, {"--output", "--time-units", "--fps", "--name",
      "--seed", "--seed-mode", "--loop", "--affine", "--palette", "--angle", "--easing",
      "--overwrite", "--replace-animation"});
  if (!options.has("--time-units"))
    throw std::invalid_argument("Timed library import requires explicit --time-units frames|seconds");
  auto input = load_document_file(args[2]);
  replacement_allowed(input, options);
  const auto units = choice<AnimationTimeUnits>(options.text("--time-units"),
      {{"frames", AnimationTimeUnits::frames}, {"seconds", AnimationTimeUnits::seconds}});
  auto clip = animation_from_timed_library(input.document, units,
      rate(options.text("--fps", "30")), options.integer<std::uint64_t>("--seed", 123456));
  settings(clip, options);
  save_clip(input, std::move(clip), input.project ? input.project->selected_index : 0,
            options, protected_files({args[2]}));
  std::cout << "Imported explicit legacy time metadata; interpolation follows Reborn's selected policies.\n";
  return 0;
}
int add_keyframe(const std::vector<fs::path> &args) {
  const auto options = arguments(args, {"--flame", "--at", "--output", "--index", "--ids",
      "--name", "--easing", "--frames", "--topology", "--replace-keyframe", "--overwrite"});
  if (!options.has("--flame") || !options.has("--at"))
    throw std::invalid_argument("animation add requires --flame INPUT and --at FRAME");
  auto input = load_document_file(args[2]);
  auto clip = project_clip(input);
  auto source = load_document_file(options.path("--flame"));
  const auto at = options.integer<std::uint32_t>("--at", 0);
  if (options.has("--frames"))
    clip.frame_count = options.integer<std::uint32_t>("--frames", 0);
  if (options.has("--topology"))
    clip.topology = topology(options.text("--topology"));
  auto key = capture_animation_keyframe(source.document, selected(source, options), at,
                                        options.text("--name"), ids(options.text("--ids")));
  key.easing = easing(options.text("--easing", "smoothstep"));
  const auto it = std::lower_bound(clip.keyframes.begin(), clip.keyframes.end(), at,
      [](const AnimationKeyframe &k, std::uint32_t frame) { return k.frame < frame; });
  if (it != clip.keyframes.end() && it->frame == at) {
    if (!options.has("--replace-keyframe"))
      throw std::invalid_argument("A keyframe already exists at that frame; use --replace-keyframe");
    *it = std::move(key);
  } else
    clip.keyframes.insert(it, std::move(key));
  save_clip(input, std::move(clip), input.project->selected_index, options,
            protected_files({args[2], options.path("--flame")}));
  return 0;
}
int inspect(const std::vector<fs::path> &args) {
  const auto options = arguments(args, {"--json"});
  const auto input = load_document_file(args[2]);
  if (!input.project)
    throw std::invalid_argument("Animation inspection requires a native .apoproject");
  if (!input.project->animation_data) {
    if (options.has("--json"))
      std::cout << "{\"present\":false,\"supported\":false}\n";
    else
      std::cout << "This native project contains no animation payload.\n";
    return 0;
  }
  std::optional<AnimationClip> clip;
  std::string issue;
  try {
    clip = parse_animation(*input.project->animation_data);
  } catch (const std::exception &error) {
    issue = error.what();
  }
  if (!clip) {
    if (options.has("--json"))
      std::cout << "{\"present\":true,\"supported\":false,\"payload_bytes\":"
                << input.project->animation_data->size() << ",\"issue\":" << json(issue) << "}\n";
    else
      std::cout << "Opaque or unsupported animation payload ("
                << input.project->animation_data->size() << " bytes): " << issue
                << "\nThe original payload remains preserved in the project.\n";
    return 0;
  }
  if (options.has("--json")) {
    std::cout << std::setprecision(17)
              << "{\"present\":true,\"supported\":true,\"version\":" << clip->version
              << ",\"name\":" << json(clip->name) << ",\"frame_count\":" << clip->frame_count
              << ",\"rate\":{\"numerator\":" << clip->rate.numerator
              << ",\"denominator\":" << clip->rate.denominator << "},\"duration_seconds\":"
              << animation_duration(*clip) << ",\"seed\":" << json(std::to_string(clip->seed))
              << ",\"seed_mode\":" << json(animation_name(clip->seed_mode))
              << ",\"loop\":" << json(animation_name(clip->loop))
              << ",\"affine\":" << json(animation_name(clip->affine))
              << ",\"palette\":" << json(animation_name(clip->palette))
              << ",\"angle\":" << json(animation_name(clip->camera_angle))
              << ",\"topology\":" << json(animation_name(clip->topology)) << ",\"keyframes\":[";
    for (std::size_t i = 0; i < clip->keyframes.size(); ++i) {
      const auto &key = clip->keyframes[i];
      if (i)
        std::cout << ',';
      std::cout << "{\"frame\":" << key.frame << ",\"name\":" << json(key.name)
                << ",\"source_index\":" << key.selected_index + 1
                << ",\"easing\":" << json(animation_name(key.easing)) << ",\"transform_ids\":[";
      for (std::size_t j = 0; j < key.transform_ids.size(); ++j) {
        if (j)
          std::cout << ',';
        std::cout << json(key.transform_ids[j]);
      }
      std::cout << "]}";
    }
    std::cout << "]}\n";
  } else {
    std::cout << "Animation version " << clip->version << ": " << clip->name << '\n'
              << "Frames: " << clip->frame_count << " (0.." << clip->frame_count - 1
              << "), rate: " << clip->rate.numerator << '/' << clip->rate.denominator
              << " fps, duration: " << animation_duration(*clip) << " seconds\n"
              << "Seed: " << clip->seed << " (" << animation_name(clip->seed_mode)
              << "), loop: " << animation_name(clip->loop) << '\n'
              << "Affine: " << animation_name(clip->affine) << ", palette: "
              << animation_name(clip->palette) << ", camera angle: "
              << animation_name(clip->camera_angle) << ", topology: "
              << animation_name(clip->topology) << '\n';
    for (const auto &key : clip->keyframes)
      std::cout << "Keyframe " << key.frame << ": " << std::quoted(key.name) << ", source "
                << key.selected_index + 1 << ", " << key.transform_ids.size()
                << " transforms, outgoing " << animation_name(key.easing) << '\n';
  }
  return 0;
}
volatile std::sig_atomic_t interrupted = 0;
extern "C" void animation_interrupt(int signal) { interrupted = signal; }
struct Interrupts {
  using Handler = void (*)(int);
  Handler interrupt, terminate;
  Interrupts() {
    interrupted = 0;
    interrupt = std::signal(SIGINT, animation_interrupt);
    if (interrupt == SIG_ERR)
      throw std::runtime_error("Cannot install animation interruption handler");
    terminate = std::signal(SIGTERM, animation_interrupt);
    if (terminate == SIG_ERR) {
      std::signal(SIGINT, interrupt);
      throw std::runtime_error("Cannot install animation termination handler");
    }
  }
  ~Interrupts() {
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, terminate);
  }
};
struct Cancellation {
  Interrupts signals;
  std::stop_source source;
  std::jthread monitor;
  Cancellation() : monitor([&](std::stop_token done) {
    while (!done.stop_requested()) {
      if (interrupted) {
        source.request_stop();
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }) {}
};
void render_settings(AnimationExportOptions &output, const Arguments &args) {
  output.width = args.integer<int>("--width", 0);
  output.height = args.integer<int>("--height", 0);
  output.render.samples = args.integer<std::uint64_t>("--samples", 1'000'000);
  output.render.threads = args.integer<unsigned>("--threads", 0);
  output.render.memory_mib = args.integer<std::uint64_t>("--memory-mib", 512);
  output.render.gpu_memory_mib = args.integer<std::uint64_t>("--gpu-memory-mib", 512);
  output.file.disk_mib = args.integer<std::uint64_t>("--disk-mib", 16384);
  output.file.strip_rows = args.integer<unsigned>("--strip-rows", 0);
  output.render.backend = choice<RenderBackend>(args.text("--backend", "cpu"),
      {{"cpu", RenderBackend::cpu}, {"auto", RenderBackend::automatic},
       {"automatic", RenderBackend::automatic}, {"vulkan", RenderBackend::vulkan}});
  output.render.device = args.text("--device");
  output.render.allow_cpu_fallback = !args.has("--no-cpu-fallback");
  output.file.image.format = parse_image_format(args.text("--format", "png8"));
  output.file.image.alpha = args.has("--alpha");
}
int render_sequence(const std::vector<fs::path> &args) {
  const auto options = arguments(args, {"--output-dir", "--state-dir", "--first", "--last",
      "--step", "--prefix", "--resume", "--overwrite", "--estimate", "--width", "--height",
      "--samples", "--threads", "--memory-mib", "--disk-mib", "--strip-rows", "--backend",
      "--gpu-memory-mib", "--device", "--no-cpu-fallback", "--format", "--alpha",
      "--pause-after-frames", "--pause-after-samples"});
  const auto input = load_document_file(args[2]);
  Cancellation cancellation;
  const auto clip = project_clip(input);
  PreparedAnimation animation(clip, cancellation.source.get_token());
  AnimationExportOptions output;
  render_settings(output, options);
  output.output_directory = options.path("--output-dir");
  output.state_directory = options.path("--state-dir");
  output.prefix = options.text("--prefix", "frame");
  output.first = options.integer<std::uint32_t>("--first", 0);
  output.last = options.integer<std::uint32_t>("--last", clip.frame_count - 1);
  output.step = options.integer<std::uint32_t>("--step", 1);
  output.resume = options.has("--resume");
  output.replace_existing = options.has("--overwrite");
  output.protected_files = protected_files({args[2]});
  output.render.stop_token = cancellation.source.get_token();
  const auto pause_frames = options.integer<std::size_t>("--pause-after-frames", 0);
  const auto pause_samples = options.integer<std::uint64_t>("--pause-after-samples", 0);
  if ((options.has("--pause-after-frames") && !pause_frames) ||
      (options.has("--pause-after-samples") && !pause_samples))
    throw std::invalid_argument("Pause thresholds must be positive");
  if (options.has("--estimate")) {
    if (pause_frames || pause_samples)
      throw std::invalid_argument("Pause controls are not applicable to --estimate");
    const auto plan = plan_animation_export(animation, output);
    std::cout << "Animation preflight: " << plan.frames.size() << " selected frames, "
              << plan.width << 'x' << plan.height << ", encoded output bound "
              << plan.output_bytes_upper_bound << " bytes, sequential disk bound "
              << plan.disk_bytes_upper_bound << " bytes\n";
    for (const auto &frame : plan.frames)
      std::cout << "Frame " << frame.frame << ", seed " << frame.seed << ": "
                << utf8(frame.destination) << (frame.completed ? " (verified complete)" : "") << '\n';
    return 0;
  }
  std::atomic<bool> paused = false;
  std::size_t completed = 0, rendered = 0;
  output.render.progress = [&](std::uint64_t done, std::uint64_t) {
    if (pause_samples && done >= pause_samples) {
      paused = true;
      cancellation.source.request_stop();
    }
  };
  output.progress = [&](const AnimationExportProgress &progress) {
    completed = progress.completed;
    if (progress.phase == "completed" || progress.phase == "verified") {
      std::cout << (progress.phase == "verified" ? "Verified" : "Wrote")
                << " frame " << progress.frame << " (" << progress.completed << '/'
                << progress.total << ")\n";
      if (progress.phase == "completed")
        ++rendered;
      if (pause_frames && rendered >= pause_frames && progress.completed < progress.total) {
        paused = true;
        cancellation.source.request_stop();
      }
    }
  };
  try {
    const auto result = export_animation_sequence(animation, output);
    std::cout << "Animation sequence complete: " << result.completed << " frames ("
              << result.rendered << " rendered, " << result.verified_existing
              << " verified); manifest " << utf8(result.manifest) << '\n';
    return 0;
  } catch (const RenderCancelled &) {
    std::cerr << (paused ? "Animation sequence paused" : "Animation sequence cancelled")
              << " after " << completed
              << " completed frames. Published images and committed checkpoints remain; resume with the same settings and --resume.\n";
    return paused ? 75 : (interrupted == SIGTERM ? 143 : 130);
  } catch (...) {
    std::cerr << "Animation sequence stopped after " << completed
              << " completed frames. Earlier images remain available; consult the diagnostic before resuming.\n";
    throw;
  }
}
int export_frame(const std::vector<fs::path> &args) {
  const auto options = arguments(args, {"--frame", "--output", "--overwrite", "--width",
      "--height", "--samples", "--threads", "--memory-mib", "--disk-mib", "--strip-rows",
      "--backend", "--gpu-memory-mib", "--device", "--no-cpu-fallback", "--format", "--alpha"});
  if (!options.has("--frame"))
    throw std::invalid_argument("animation frame requires --frame N");
  const auto input = load_document_file(args[2]);
  Cancellation cancellation;
  PreparedAnimation animation(project_clip(input), cancellation.source.get_token());
  const auto evaluated = animation.evaluate(options.integer<std::int64_t>("--frame", 0));
  AnimationExportOptions output;
  render_settings(output, options);
  if ((output.width == 0) != (output.height == 0) || output.width < 0 || output.height < 0)
    throw std::invalid_argument("Specify both positive width and height");
  const auto flame = animation_export_flame(evaluated,
      output.width ? output.width : evaluated.flame.width,
      output.height ? output.height : evaluated.flame.height);
  const auto destination = output_path(options.path("--output"), options.has("--overwrite"),
                                        protected_files({args[2]}));
  const bool prior_exists = fs::exists(destination);
  const auto prior_sha = prior_exists ? detail::sha256_file(destination, cancellation.source.get_token())
                                       : std::string{};
  const auto before_publish = [&] {
    if (cancellation.source.stop_requested())
      throw RenderCancelled();
    const auto checked = output_path(options.path("--output"), true, protected_files({args[2]}));
    if (checked != destination || fs::exists(destination) != prior_exists ||
        (prior_exists && detail::sha256_file(destination, cancellation.source.get_token()) != prior_sha))
      throw std::invalid_argument("Animation frame destination changed during evaluation/rendering");
  };
  auto extension = destination.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                  [](unsigned char c) { return char(std::tolower(c)); });
  if (extension == ".flame") {
    for (const auto &option : {"--samples", "--threads", "--memory-mib", "--disk-mib",
                              "--strip-rows", "--backend", "--gpu-memory-mib", "--device",
                              "--no-cpu-fallback", "--format", "--alpha"})
      if (options.has(option))
        throw std::invalid_argument("Image rendering options cannot be used with a .flame frame export");
    const auto bytes = serialize_flame(flame);
    write_atomic(destination, [&](std::ostream &out) { out.write(bytes.data(), bytes.size()); },
                 before_publish);
  } else {
    if (!options.has("--format"))
      output.file.image.format = image_format_for_path(destination);
    else if (image_format_for_path(destination, image_format_bits(output.file.image.format)) !=
             output.file.image.format)
      throw std::invalid_argument("Frame filename does not match the chosen image format");
    output.render.seed = evaluated.seed;
    output.render.stop_token = cancellation.source.get_token();
    output.file.before_publish = before_publish;
    (void)plan_file_render(flame, output.render, output.file);
    try {
      const auto result = render_to_file(flame, output.render, destination, output.file);
      std::cout << "Renderer: " << backend_name(result.backend) << ", " << result.device_name << '\n';
    } catch (const RenderCancelled &) {
      std::cerr << "Animation frame render cancelled; the destination was preserved.\n";
      return interrupted == SIGTERM ? 143 : 130;
    }
  }
  std::cout << std::setprecision(17) << "Wrote frame " << evaluated.frame << " at "
            << animation_seconds(animation.clip(), evaluated.frame) << " seconds, seed "
            << evaluated.seed << ": " << utf8(destination) << '\n';
  return 0;
}
} // namespace

std::optional<int> animation_command(const std::vector<fs::path> &args) {
  if (args.empty() || args[0] != "animation")
    return {};
  if (args.size() == 1 || (args.size() == 2 && (args[1] == "--help" || args[1] == "-h"))) {
    animation_usage(std::cout);
    return 0;
  }
  if (args.size() < 3)
    throw std::invalid_argument("animation requires a command and input file; see animation --help");
  const auto command = utf8(args[1]);
  try {
    if (command == "create" || command == "preset")
      return create(args, command == "preset");
    if (command == "morph")
      return morph(args);
    if (command == "import")
      return import_timed(args);
    if (command == "add")
      return add_keyframe(args);
    if (command == "inspect")
      return inspect(args);
    if (command == "frame")
      return export_frame(args);
    if (command == "render")
      return render_sequence(args);
    throw std::invalid_argument("Unknown animation command " + command);
  } catch (const AnimationCancelled &) {
    std::cerr << "Animation preparation cancelled; previously committed outputs remain available.\n";
    return interrupted == SIGTERM ? 143 : 130;
  } catch (const RenderCancelled &) {
    std::cerr << "Animation operation cancelled; previously committed outputs remain available.\n";
    return interrupted == SIGTERM ? 143 : 130;
  }
}
void animation_usage(std::ostream &out) {
  out << "\nAnimation and image sequences:\n"
      << "  animation create INPUT --output clip.apoproject [--preset still|camera-orbit|zoom|palette-cycle]\n"
      << "  animation preset INPUT --preset NAME --output clip.apoproject [clip options]\n"
      << "  animation morph FROM --to TO --output clip.apoproject [--from-ids A,B --to-ids A,B]\n"
      << "  animation import INPUT --time-units frames|seconds --output clip.apoproject\n"
      << "  animation add PROJECT --flame INPUT --at N --output edited.apoproject [--replace-keyframe]\n"
      << "  animation inspect PROJECT [--json]\n"
      << "  animation frame PROJECT --frame N --output frame.flame|frame.png [render options]\n"
      << "  animation render PROJECT --output-dir DIR [--first N --last N --step N --prefix NAME]\n"
      << "    [--state-dir DIR --resume --overwrite --estimate --pause-after-frames N --pause-after-samples N]\n"
      << "  Clip options: --frames N --fps N[/D] --seed UINT64 --seed-mode fixed|per-frame\n"
      << "    --loop once|repeat|ping-pong --easing hold|linear|smoothstep|smootherstep\n"
      << "    --affine polar|linear --palette rgb|hsv-shortest|linear-rgb|hsl-shortest\n"
      << "    --angle shortest|unwrapped --topology strict|fade-unmatched --name TEXT\n"
      << "  Render options: --width W --height H --samples N --threads N --memory-mib N\n"
      << "    --disk-mib N --strip-rows N --backend cpu|auto|vulkan --device ID\n"
      << "    --gpu-memory-mib N --no-cpu-fallback --format png8|png16|tiff8|tiff16|ppm8|ppm16|bmp8 --alpha\n"
      << "  Source indices start at 1; animation frames start at 0. Ranges are inclusive.\n"
      << "  Native saves require a distinct .apoproject destination; --overwrite permits file replacement.\n"
      << "  Replacing existing animation payloads also requires --replace-animation.\n"
      << "  Export batches contain at most 1000 selected frames. No video encoder is required.\n";
}
} // namespace apo
