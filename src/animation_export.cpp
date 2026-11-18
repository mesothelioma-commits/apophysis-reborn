#include "animation_export.hpp"
#include "flame_io.hpp"
#include "gpu.hpp"
#include "output_file.hpp"
#include "plugin.hpp"
#include "script_host.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <charconv>
#include <climits>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace apo {
namespace {
namespace fs = std::filesystem;
constexpr std::uint64_t mib = 1048576;
constexpr std::size_t manifest_limit = 8 * 1024 * 1024;
void cancel(std::stop_token stop) {
  if (stop.stop_requested())
    throw RenderCancelled();
}
std::string utf8(const fs::path &path) {
  const auto s = path.u8string();
  return {s.begin(), s.end()};
}
std::uint64_t sum(std::uint64_t a, std::uint64_t b) {
  if (a > UINT64_MAX - b)
    throw std::invalid_argument("Animation storage estimate overflows");
  return a + b;
}
std::uint64_t product(std::uint64_t a, std::uint64_t b) {
  if (b && a > UINT64_MAX / b)
    throw std::invalid_argument("Animation storage estimate overflows");
  return a * b;
}
bool same_path(const fs::path &a, const fs::path &b) {
#ifdef _WIN32
  const auto &x = a.native(), &y = b.native();
  if (x.size() > INT_MAX || y.size() > INT_MAX)
    throw std::invalid_argument("Animation path is too long");
  return CompareStringOrdinal(x.data(), int(x.size()), y.data(), int(y.size()),
                              TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
bool contains(const fs::path &parent, const fs::path &child) {
  auto p = parent.begin(), c = child.begin();
  for (; p != parent.end(); ++p, ++c)
    if (c == child.end() || !same_path(*p, *c))
      return false;
  return true;
}
void reject_symlinks(const fs::path &path) {
  fs::path part;
  for (const auto &component : fs::absolute(path).lexically_normal()) {
    part /= component;
    if (fs::is_symlink(fs::symlink_status(part)))
      throw std::invalid_argument("Animation state/output cannot use a symlink: " +
                                  utf8(part));
  }
}
void protect_state(const fs::path &state, const std::vector<fs::path> &files) {
  for (const auto &file : files) {
    if (file.empty())
      continue;
    const auto protected_path = fs::weakly_canonical(fs::absolute(file));
    if (contains(state, protected_path) || contains(protected_path, state) ||
        (fs::exists(state) && fs::exists(protected_path) &&
         fs::equivalent(state, protected_path)))
      throw std::invalid_argument(
          "Animation state directory overlaps an input or native module");
  }
}
void protect_plugins(const Flame &flame, std::vector<fs::path> &files) {
  auto add = [&](const Transform &transform) {
    for (const auto &term : transform.variations)
      if (term.plugin) {
        files.push_back(term.plugin->info().manifest_path);
        files.push_back(term.plugin->info().library_path);
      }
  };
  for (const auto &transform : flame.transforms)
    add(transform);
  if (flame.final_transform)
    add(*flame.final_transform);
}
std::string model_identity(const Flame &flame) {
  std::ostringstream out;
  out << serialize_flame(flame) << "\nplugins\n" << plugin_identity(flame);
  for (const auto &t : flame.transforms)
    out << '\n' << t.parameters.apophysis_math << ' '
        << t.parameters.separate_blur_angle;
  if (flame.final_transform)
    out << "\nfinal " << flame.final_transform->parameters.apophysis_math << ' '
        << flame.final_transform->parameters.separate_blur_angle;
  return detail::sha256_bytes(out.str());
}
std::string frame_name(std::string_view prefix, std::uint32_t frame,
                       ImageFormat format) {
  std::ostringstream out;
  out << prefix << '_' << std::setfill('0') << std::setw(6) << frame
      << image_format_extension(format);
  return out.str();
}
std::string state_frame(std::uint32_t frame) {
  std::ostringstream out;
  out << "frame_" << std::setfill('0') << std::setw(6) << frame;
  return out.str();
}
std::string renderer_identity(std::stop_token stop) {
  // Bind resume to the executable containing the renderer, including compiled
  // shader bytes. Rebuilding/upgrading requires a new sequence state directory.
#ifdef _WIN32
  std::wstring buffer(32768, L'\0');
  const auto count = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
  if (!count || count == buffer.size())
    throw std::runtime_error("Cannot identify the animation renderer executable");
  buffer.resize(count);
  return detail::sha256_file(fs::path(buffer), stop, 512 * mib);
#elif defined(__linux__)
  return detail::sha256_file(fs::canonical("/proc/self/exe"), stop, 512 * mib);
#else
  return detail::sha256_bytes(std::string(__VERSION__) + " " + __DATE__ + " " +
                              __TIME__);
#endif
}
struct FileStamp {
  bool exists = false;
  std::uint64_t size = 0;
  std::string sha;
  bool operator==(const FileStamp &) const = default;
};
FileStamp stamp(const fs::path &path, std::stop_token stop) {
  cancel(stop);
  if (fs::is_symlink(fs::symlink_status(path)))
    throw std::invalid_argument("Animation file cannot be a symlink: " + utf8(path));
  if (!fs::exists(path))
    return {};
  if (!fs::is_regular_file(path))
    throw std::invalid_argument("Animation output is not a regular file: " + utf8(path));
  const auto size = fs::file_size(path);
  return {true, size, detail::sha256_file(path, stop)};
}
struct Record {
  std::uint32_t frame = 0;
  std::uint64_t seed = 0;
  std::string filename, model;
  unsigned phase = 0; // Pending, durably rendered, durably published.
  FileStamp prior, output;
  std::string backend, device;
};
struct Manifest {
  std::string identity;
  std::vector<Record> frames;
};
std::string manifest_bytes(const Manifest &manifest) {
  std::ostringstream out;
  out << "APO-SEQUENCE-1\n" << std::quoted(manifest.identity) << '\n'
      << manifest.frames.size() << '\n';
  for (const auto &r : manifest.frames)
    out << r.frame << ' ' << r.seed << ' ' << std::quoted(r.filename) << ' '
        << std::quoted(r.model) << ' ' << r.phase << ' ' << r.prior.exists << ' '
        << r.prior.size << ' ' << std::quoted(r.prior.sha) << ' ' << r.output.size
        << ' ' << std::quoted(r.output.sha) << ' ' << std::quoted(r.backend) << ' '
        << std::quoted(r.device) << '\n';
  const auto body = out.str();
  return body + "SHA256 " + detail::sha256_bytes(body) + '\n';
}
bool is_digest(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
template <class T> T read_number(std::istream &in) {
  std::string value;
  T result{};
  if (!(in >> value))
    throw std::invalid_argument("Truncated animation sequence manifest");
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size() || value.empty())
    throw std::invalid_argument("Invalid number in animation sequence manifest");
  return result;
}
std::string read_manifest_bytes(const fs::path &path) {
  if (!fs::is_regular_file(path) || fs::is_symlink(path))
    throw std::invalid_argument("Animation sequence manifest is missing or unsafe");
  const auto size = fs::file_size(path);
  if (size > manifest_limit)
    throw std::invalid_argument("Animation sequence manifest exceeds its size limit");
  std::ifstream in(path, std::ios::binary);
  std::string bytes(std::size_t(size), '\0');
  if (!in.read(bytes.data(), std::streamsize(size)) || in.peek() != EOF)
    throw std::runtime_error("Cannot read the complete animation sequence manifest");
  return bytes;
}
Manifest parse_manifest(std::string_view bytes) {
  const auto trailer = bytes.rfind("\nSHA256 ");
  if (trailer == std::string_view::npos || bytes.size() != trailer + 8 + 64 + 1 ||
      bytes.back() != '\n' ||
      detail::sha256_bytes(bytes.substr(0, trailer + 1)) != bytes.substr(trailer + 8, 64))
    throw std::invalid_argument("Animation sequence manifest checksum is invalid");
  std::istringstream in(std::string(bytes.substr(0, trailer + 1)));
  std::string magic;
  Manifest result;
  if (!std::getline(in, magic) || magic != "APO-SEQUENCE-1" ||
      !(in >> std::quoted(result.identity)) || !is_digest(result.identity))
    throw std::invalid_argument("Unsupported animation sequence manifest");
  const auto count = read_number<std::size_t>(in);
  if (!count || count > animation_export_frame_limit)
    throw std::invalid_argument("Invalid frame count in animation sequence manifest");
  for (std::size_t i = 0; i < count; ++i) {
    Record r;
    r.frame = read_number<std::uint32_t>(in);
    r.seed = read_number<std::uint64_t>(in);
    if (!(in >> std::quoted(r.filename) >> std::quoted(r.model)))
      throw std::invalid_argument("Truncated animation sequence manifest");
    r.phase = read_number<unsigned>(in);
    const auto prior = read_number<unsigned>(in);
    r.prior.exists = prior == 1;
    r.prior.size = read_number<std::uint64_t>(in);
    if (!(in >> std::quoted(r.prior.sha)))
      throw std::invalid_argument("Truncated animation sequence manifest");
    r.output.size = read_number<std::uint64_t>(in);
    if (!(in >> std::quoted(r.output.sha) >> std::quoted(r.backend) >>
          std::quoted(r.device)))
      throw std::invalid_argument("Truncated animation sequence manifest");
    r.output.exists = r.phase != 0;
    if (r.phase > 2 || prior > 1 || r.frame >= animation_frame_limit ||
        r.filename.empty() || r.filename.size() > 128 || !is_digest(r.model) ||
        (r.prior.exists ? !is_digest(r.prior.sha)
                        : (r.prior.size != 0 || !r.prior.sha.empty())) ||
        (r.phase ? (!r.output.size || !is_digest(r.output.sha) ||
                    (r.backend != "cpu" && r.backend != "vulkan"))
                 : (r.output.size || !r.output.sha.empty() || !r.backend.empty() ||
                    !r.device.empty())) ||
        r.device.size() > 4096)
      throw std::invalid_argument("Invalid fields in animation sequence manifest");
    result.frames.push_back(std::move(r));
  }
  in >> std::ws;
  if (!in.eof())
    throw std::invalid_argument("Trailing fields in animation sequence manifest");
  return result;
}
void sync_file(const fs::path &path) {
#ifdef _WIN32
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot flush animation sequence file");
  const bool ok = FlushFileBuffers(h) != 0;
  CloseHandle(h);
  if (!ok)
    throw std::runtime_error("Cannot flush animation sequence file");
#else
  int h = ::open(path.c_str(), O_RDONLY);
  if (h < 0)
    throw std::runtime_error("Cannot flush animation sequence file");
  const int status = ::fsync(h);
  ::close(h);
  if (status)
    throw std::runtime_error("Cannot flush animation sequence file");
  h = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (h < 0)
    throw std::runtime_error("Cannot flush animation sequence directory");
  const int directory_status = ::fsync(h);
  ::close(h);
  if (directory_status)
    throw std::runtime_error("Cannot flush animation sequence directory");
#endif
}
void save_manifest(const fs::path &path, const Manifest &manifest) {
  const auto bytes = manifest_bytes(manifest);
  if (bytes.size() > manifest_limit)
    throw std::invalid_argument("Animation sequence manifest exceeds its size limit");
  write_atomic(path, [&](std::ostream &out) { out.write(bytes.data(), bytes.size()); });
  sync_file(path);
}
class SequenceLock {
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int handle_ = -1;
#endif
public:
  explicit SequenceLock(const fs::path &path) {
    reject_symlinks(path);
#ifdef _WIN32
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
#else
    handle_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (handle_ < 0 || flock(handle_, LOCK_EX | LOCK_NB))
#endif
    {
#ifndef _WIN32
      if (handle_ >= 0)
        ::close(handle_);
      handle_ = -1;
#endif
      throw std::runtime_error("Animation sequence state is in use or cannot be locked");
    }
  }
  ~SequenceLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
#else
    if (handle_ >= 0)
      ::close(handle_);
#endif
  }
  SequenceLock(const SequenceLock &) = delete;
  SequenceLock &operator=(const SequenceLock &) = delete;
};
struct Planning {
  AnimationExportPlan plan;
  Manifest manifest;
  std::string loaded_manifest_sha;
  std::vector<fs::path> protected_files;
};
void verify_record(const Record &record, const fs::path &destination,
                   const fs::path &staged, std::stop_token stop) {
  const auto current = stamp(destination, stop);
  if (record.phase == 2) {
    if (current != record.output)
      throw std::invalid_argument("Completed animation frame is missing or changed: " +
                                  utf8(destination));
  } else if (record.phase == 1 && current == record.output) {
    // Publication finished before the completed journal entry reached disk.
  } else {
    if (current != record.prior)
      throw std::invalid_argument("Animation destination changed since sequence preflight: " +
                                  utf8(destination));
    if (record.phase == 1 && stamp(staged, stop) != record.output)
      throw std::invalid_argument("Staged animation frame is missing or changed: " +
                                  utf8(staged));
  }
}
Planning prepare(const PreparedAnimation &animation,
                 const AnimationExportOptions &options) {
  cancel(options.render.stop_token);
  if (options.file.resume || !options.file.checkpoint_directory.empty())
    throw std::invalid_argument("The animation host owns per-frame checkpoint paths; use sequence resume");
  if (!options.step || options.first > options.last ||
      options.last >= animation.clip().frame_count)
    throw std::invalid_argument("Animation range requires 0 <= first <= last < frame count and step >= 1");
  const auto count = std::uint64_t(options.last - options.first) / options.step + 1;
  if (count > animation_export_frame_limit)
    throw std::invalid_argument("Animation export is limited to 1000 selected frames per batch; choose an explicit smaller range");
  if (options.prefix.empty() || options.prefix.size() > 96 ||
      !std::all_of(options.prefix.begin(), options.prefix.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
      }))
    throw std::invalid_argument("Animation prefix must contain 1..96 ASCII letters, digits, hyphens or underscores");
  if (options.output_directory.empty() || !fs::is_directory(options.output_directory))
    throw std::invalid_argument("Choose an existing animation output directory");
  if ((options.width == 0) != (options.height == 0) || options.width < 0 || options.height < 0)
    throw std::invalid_argument("Specify both positive animation width and height, or leave both unset");
  Planning result;
  auto &plan = result.plan;
  plan.output_directory = fs::canonical(options.output_directory);
  const auto state = options.state_directory.empty()
                         ? plan.output_directory / (".apo-sequence-" + options.prefix)
                         : fs::absolute(options.state_directory).lexically_normal();
  reject_symlinks(state);
  plan.state_directory = fs::weakly_canonical(state);
  if (fs::exists(plan.state_directory) && !fs::is_directory(plan.state_directory))
    throw std::invalid_argument("Animation state path is not a directory");
  if (options.resume) {
    if (!fs::is_directory(plan.state_directory))
      throw std::invalid_argument("Animation sequence state is missing; resume cannot infer completed files");
  } else if (fs::exists(plan.state_directory) && !fs::is_empty(plan.state_directory)) {
    throw std::invalid_argument("Animation sequence state already exists; use --resume or a new state directory");
  }
  result.protected_files = options.protected_files;
  // Explicitly enabled modules are also protected, even if a particular frame
  // currently has zero contribution from that module.
  for (const auto &plugin : enabled_plugins()) {
    result.protected_files.push_back(plugin.manifest_path);
    result.protected_files.push_back(plugin.library_path);
  }
  std::optional<ComputeInfo> devices;
  if (options.render.backend != RenderBackend::cpu) {
    devices = compute_devices();
    if (!options.render.allow_cpu_fallback) {
      unsigned requested_index = 0;
      const auto &identifier = options.render.device;
      const auto parsed = std::from_chars(identifier.data(), identifier.data() + identifier.size(),
                                           requested_index);
      const bool numeric = !identifier.empty() && parsed.ec == std::errc{} &&
                           parsed.ptr == identifier.data() + identifier.size();
      const bool available = std::any_of(devices->devices.begin(), devices->devices.end(),
          [&](const ComputeDevice &device) {
            const bool selected = options.render.device.empty() ||
                options.render.device == device.id ||
                (numeric && requested_index == device.index);
            return selected && device.usable &&
                !(options.render.device.empty() &&
                  options.render.backend == RenderBackend::automatic && device.software);
          });
      if (!devices->compiled || !devices->loader_available || !available)
        throw std::invalid_argument("The requested animation Vulkan renderer is unavailable and CPU fallback is disabled");
    }
  }
  ScriptResult requests;
  std::uint64_t peak_scratch = 0;
  const auto first_frame = animation.evaluate(options.first);
  plan.width = options.width ? options.width : first_frame.flame.width;
  plan.height = options.height ? options.height : first_frame.flame.height;
  const auto image_bound = image_export_upper_bound(plan.width, plan.height, options.file.image);
  for (std::uint64_t index = options.first; index <= options.last; index += options.step) {
    cancel(options.render.stop_token);
    const auto evaluated = animation.evaluate(std::int64_t(index));
    auto flame = animation_export_flame(evaluated, plan.width, plan.height);
    protect_plugins(flame, result.protected_files);
    if (devices && !options.render.allow_cpu_fallback) {
      if (has_plugin_variations(flame))
        throw std::invalid_argument("Animation native variations require CPU rendering or explicit CPU fallback");
      if (const auto reason = compute_support_reason(flame); !reason.empty())
        throw std::invalid_argument("Animation frame cannot use the requested Vulkan renderer: " + reason);
    }
    auto render = options.render;
    render.seed = evaluated.seed;
    auto frame_plan = plan_file_render(flame, render, options.file);
    const auto filename = frame_name(options.prefix, std::uint32_t(index), options.file.image.format);
    ScriptRequest request;
    request.kind = ScriptRequestKind::export_flame;
    request.destination = fs::path(filename);
    request.bytes = "animation frame";
    requests.requests.push_back(std::move(request));
    const auto model = model_identity(flame);
    plan.frames.push_back({std::uint32_t(index), evaluated.seed, {}, frame_plan, model, false});
    result.manifest.frames.push_back({std::uint32_t(index), evaluated.seed, filename, model,
                                      0, {}, {}, {}, {}});
    plan.output_bytes_upper_bound = sum(plan.output_bytes_upper_bound, image_bound);
    // The file renderer's own estimate covers its output staging. Publication
    // needs one extra encoded image; committed frame checkpoints are removed.
    peak_scratch = std::max(peak_scratch, sum(frame_plan.disk_bytes, image_bound));
  }
  std::sort(result.protected_files.begin(), result.protected_files.end());
  result.protected_files.erase(std::unique(result.protected_files.begin(), result.protected_files.end()),
                               result.protected_files.end());
  protect_state(plan.state_directory, result.protected_files);
  result.protected_files.push_back(plan.state_directory);
  const auto paths = plan_script_outputs(requests, plan.output_directory,
                                         options.replace_existing || options.resume,
                                         result.protected_files);
  for (std::size_t i = 0; i < paths.size(); ++i) {
    reject_symlinks(plan.output_directory / requests.requests[i].destination);
    plan.frames[i].destination = paths[i];
  }
  plan.disk_bytes_upper_bound = sum(sum(plan.output_bytes_upper_bound, peak_scratch), manifest_limit);
  if (plan.disk_bytes_upper_bound > product(options.file.disk_mib, mib))
    throw std::invalid_argument("The complete animation batch exceeds --disk-mib (final images plus peak sequence scratch)");
  std::ostringstream identity;
  identity << "Apophysis-Reborn-sequence-1 / stream-1\n" << renderer_identity(options.render.stop_token)
           << '\n' << __VERSION__ << '\n' << sizeof(void *) << ' ' << std::fegetround()
#ifdef _WIN32
           << " Windows\n"
#else
           << " POSIX\n"
#endif
           << std::quoted(utf8(plan.output_directory)) << '\n'
           << detail::sha256_bytes(serialize_animation(animation.clip())) << '\n'
           << options.first << ' ' << options.last << ' ' << options.step << ' '
           << plan.width << ' ' << plan.height << '\n'
           << options.render.samples << ' ' << int(options.render.backend) << ' '
           << std::quoted(options.render.device) << ' ' << options.render.allow_cpu_fallback
           << ' ' << int(options.file.image.format) << ' ' << options.file.image.alpha << '\n';
  if (devices) {
    // Resource ceilings can change GPU admission/fallback, so they remain
    // immutable for non-CPU sequences. CPU thread/memory ceilings can change.
    identity << "compute " << devices->compiled << ' ' << devices->loader_available
             << ' ' << options.render.memory_mib << ' ' << options.render.gpu_memory_mib << '\n';
    for (const auto &device : devices->devices)
      identity << std::quoted(device.id) << ' ' << std::quoted(device.name) << ' '
               << std::quoted(device.api_version) << ' ' << std::quoted(device.driver_version)
               << ' ' << device.usable << ' ' << device.software << ' '
               << device.float64 << ' ' << device.int64 << '\n';
  }
  for (const auto &record : result.manifest.frames)
    identity << record.frame << ' ' << record.seed << ' ' << std::quoted(record.filename)
             << ' ' << record.model << '\n';
  plan.identity_sha256 = detail::sha256_bytes(identity.str());
  result.manifest.identity = plan.identity_sha256;
  if (options.resume) {
    const auto bytes = read_manifest_bytes(plan.state_directory / "manifest.aposequence");
    result.loaded_manifest_sha = detail::sha256_bytes(bytes);
    auto saved = parse_manifest(bytes);
    if (saved.identity != result.manifest.identity || saved.frames.size() != plan.frames.size())
      throw std::invalid_argument("Animation resume identity mismatch: clip, renderer/module, range, names, canvas or render settings changed");
    for (std::size_t i = 0; i < plan.frames.size(); ++i) {
      const auto &expected = result.manifest.frames[i], &record = saved.frames[i];
      if (record.frame != expected.frame || record.seed != expected.seed ||
          record.filename != expected.filename || record.model != expected.model)
        throw std::invalid_argument("Animation resume frame mapping does not match its manifest");
      const auto staged = plan.state_directory / "rendered" / fs::path(record.filename);
      const auto checkpoint = plan.state_directory / "checkpoints" / state_frame(record.frame);
      reject_symlinks(staged);
      reject_symlinks(checkpoint);
      verify_record(record, paths[i], staged, options.render.stop_token);
      if (record.phase == 0 && fs::exists(checkpoint) && !fs::is_empty(checkpoint) &&
          !has_render_checkpoint(checkpoint))
        throw std::invalid_argument("Animation frame checkpoint is incomplete or corrupt: " + utf8(checkpoint));
      plan.frames[i].completed = record.phase == 2;
    }
    result.manifest = std::move(saved);
  } else {
    for (std::size_t i = 0; i < plan.frames.size(); ++i)
      result.manifest.frames[i].prior = stamp(paths[i], options.render.stop_token);
  }
  return result;
}
void publish(const fs::path &staged, const fs::path &destination, const Record &record,
             const AnimationExportOptions &options) {
  if (stamp(staged, options.render.stop_token) != record.output)
    throw std::invalid_argument("Staged animation frame changed before publication");
  write_atomic(destination, [&](std::ostream &out) {
    std::ifstream in(staged, std::ios::binary);
    if (!in)
      throw std::runtime_error("Cannot read the staged animation frame");
    detail::Sha256 hash;
    std::array<char, 65536> bytes{};
    std::uint64_t copied = 0;
    while (in) {
      cancel(options.render.stop_token);
      in.read(bytes.data(), bytes.size());
      const auto n = in.gcount();
      if (n > 0) {
        copied = sum(copied, std::uint64_t(n));
        if (copied > record.output.size)
          throw std::runtime_error("Staged animation frame grew during publication");
        hash.add(reinterpret_cast<const unsigned char *>(bytes.data()), std::size_t(n));
        out.write(bytes.data(), n);
      }
    }
    if (!in.eof() || copied != record.output.size || hash.finish() != record.output.sha)
      throw std::runtime_error("Staged animation frame changed during publication");
  }, [&] {
    cancel(options.render.stop_token);
    if (options.file.before_publish)
      options.file.before_publish();
    reject_symlinks(destination);
    if (stamp(destination, options.render.stop_token) != record.prior)
      throw std::invalid_argument("Animation destination changed before publication: " + utf8(destination));
  });
  sync_file(destination);
}
} // namespace

Flame animation_export_flame(const AnimationFrame &frame, int width, int height) {
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("Animation canvas dimensions must be positive");
  auto flame = frame.flame;
  if (flame.width <= 0 || flame.height <= 0)
    throw std::invalid_argument("Animation source canvas dimensions must be positive");
  flame.scale *= std::min(double(width) / flame.width, double(height) / flame.height);
  flame.width = width;
  flame.height = height;
  validate(flame);
  return flame;
}
AnimationExportPlan plan_animation_export(const PreparedAnimation &animation,
                                          const AnimationExportOptions &options) {
  return prepare(animation, options).plan;
}
AnimationExportResult export_animation_sequence(const PreparedAnimation &animation,
                                                 const AnimationExportOptions &options) {
  gpu::ContextScope contextScope;
  auto prepared = prepare(animation, options);
  auto &plan = prepared.plan;
  auto &manifest = prepared.manifest;
  const auto manifest_path = plan.state_directory / "manifest.aposequence";
  cancel(options.render.stop_token);
  reject_symlinks(plan.state_directory);
  fs::create_directories(plan.state_directory);
  SequenceLock lock(plan.state_directory / "sequence.lock");
  if (options.resume) {
    if (detail::sha256_bytes(read_manifest_bytes(manifest_path)) != prepared.loaded_manifest_sha)
      throw std::runtime_error("Animation sequence state changed during preflight; retry resume");
  } else {
    for (const auto &entry : fs::directory_iterator(plan.state_directory))
      if (entry.path().filename() != "sequence.lock")
        throw std::runtime_error("Animation sequence state changed during preflight; choose a new state directory");
    save_manifest(manifest_path, manifest);
  }
  reject_symlinks(plan.state_directory / "rendered");
  reject_symlinks(plan.state_directory / "checkpoints");
  fs::create_directories(plan.state_directory / "rendered");
  fs::create_directories(plan.state_directory / "checkpoints");
  AnimationExportResult result;
  result.manifest = manifest_path;
  auto progress = [&](std::uint32_t frame, std::string phase,
                      std::optional<FileRenderProgress> render = {}) {
    if (options.progress)
      options.progress({frame, result.completed, plan.frames.size(), std::move(phase),
                        std::move(render)});
  };
  for (std::size_t i = 0; i < manifest.frames.size(); ++i) {
    cancel(options.render.stop_token);
    auto &record = manifest.frames[i];
    const auto destination = plan.frames[i].destination;
    const auto staged = plan.state_directory / "rendered" / fs::path(record.filename);
    const auto checkpoint = plan.state_directory / "checkpoints" / state_frame(record.frame);
    reject_symlinks(staged);
    reject_symlinks(checkpoint);
    verify_record(record, destination, staged, options.render.stop_token);
    if (record.phase == 2) {
      ++result.completed;
      ++result.verified_existing;
      progress(record.frame, "verified");
      fs::remove(staged);
      fs::remove_all(checkpoint);
      continue;
    }
    if (record.phase == 0) {
      progress(record.frame, "rendering");
      const auto evaluated = animation.evaluate(record.frame);
      auto flame = animation_export_flame(evaluated, plan.width, plan.height);
      if (model_identity(flame) != record.model || evaluated.seed != record.seed)
        throw std::runtime_error("Animation frame changed after sequence preflight");
      auto render = options.render;
      render.seed = record.seed;
      auto file = options.file;
      // The caller's guard concerns the public destination, not private staging.
      file.before_publish = {};
      file.checkpoint_directory = checkpoint;
      file.resume = fs::exists(checkpoint) && !fs::is_empty(checkpoint);
      if (file.resume && !has_render_checkpoint(checkpoint))
        throw std::invalid_argument("Animation frame checkpoint is incomplete or corrupt: " + utf8(checkpoint));
      file.status = [&](const FileRenderProgress &status) {
        if (options.file.status)
          options.file.status(status);
        progress(record.frame, status.phase, status);
      };
      const auto rendered = render_to_file(flame, render, staged, file);
      sync_file(staged);
      record.output = stamp(staged, options.render.stop_token);
      record.backend = rendered.backend == RenderBackend::vulkan ? "vulkan" : "cpu";
      record.device = rendered.device_name;
      record.phase = 1;
      save_manifest(manifest_path, manifest);
      ++result.rendered;
      progress(record.frame, "ready");
    }
    cancel(options.render.stop_token);
    const auto current = stamp(destination, options.render.stop_token);
    if (current != record.output) {
      if (current != record.prior)
        throw std::invalid_argument("Animation destination changed before publication: " + utf8(destination));
      publish(staged, destination, record, options);
    }
    record.phase = 2;
    save_manifest(manifest_path, manifest);
    ++result.completed;
    progress(record.frame, "completed");
    // Removal follows the durable completed record. A crash before cleanup is
    // harmless; resume still verifies the published frame instead of trusting
    // the presence of either a staged image or an old checkpoint.
    fs::remove(staged);
    fs::remove_all(checkpoint);
  }
  return result;
}
} // namespace apo
