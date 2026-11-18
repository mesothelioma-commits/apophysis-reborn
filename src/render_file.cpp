#include "render_file.hpp"
#include "plugin.hpp"
#include "render_internal.hpp"
#include "flame_io.hpp"
#include "output_file.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <chrono>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>
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
using Clock = std::chrono::steady_clock;
using Hit = detail::SampleHit;
constexpr std::uint64_t MiB = 1024 * 1024, batch_size = 8192;
constexpr std::uint64_t hash_start = UINT64_C(14695981039346656037);
constexpr std::uint64_t hash_prime = UINT64_C(1099511628211);
void cancel(std::stop_token stop) { if (stop.stop_requested()) throw RenderCancelled(); }
std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > UINT64_MAX / b) throw std::invalid_argument("Render storage estimate overflows");
    return a * b;
}
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (a > UINT64_MAX - b) throw std::invalid_argument("Render storage estimate overflows");
    return a + b;
}
std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) { return a / b + (a % b != 0); }
std::uint64_t hash_bytes(std::uint64_t hash, const char* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) hash = (hash ^ static_cast<unsigned char>(data[i])) * hash_prime;
    return hash;
}
void put64(std::ostream& out, std::uint64_t value) {
    std::array<char, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i) bytes[i] = char(value >> (8 * i));
    out.write(bytes.data(), bytes.size());
}
std::uint64_t get64(std::istream& in) {
    std::array<unsigned char, 8> bytes{};
    if (!in.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Truncated render checkpoint");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(bytes[i]) << (8 * i);
    return value;
}
std::array<char, 24> encode(const Hit& hit) {
    std::array<char, 24> bytes{};
    for (unsigned c = 0; c < 6; ++c)
        for (unsigned i = 0; i < 4; ++i) bytes[c * 4 + i] = char(hit[c] >> (8 * i));
    return bytes;
}
Hit decode(const char* bytes) {
    Hit hit{};
    for (unsigned c = 0; c < 6; ++c)
        for (unsigned i = 0; i < 4; ++i)
            hit[c] |= std::uint32_t(static_cast<unsigned char>(bytes[c * 4 + i])) << (8 * i);
    return hit;
}

// File locks are released by the OS on process termination. The lock file itself
// is retained, avoiding stale-PID guesses and unlink/reopen races.
class CheckpointLock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
  public:
    explicit CheckpointLock(const fs::path& path) {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
#else
        handle = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (handle < 0 || flock(handle, LOCK_EX | LOCK_NB) != 0)
#endif
        {
#ifndef _WIN32
            if (handle >= 0) ::close(handle);
            handle = -1;
#endif
            throw std::runtime_error("Render checkpoint is in use or cannot be locked");
        }
    }
    ~CheckpointLock() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (handle >= 0) ::close(handle);
#endif
    }
};

void sync_file(const fs::path& path) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot flush render checkpoint");
    const bool ok = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    if (!ok) throw std::runtime_error("Cannot flush render checkpoint");
#else
    int handle = ::open(path.c_str(), O_RDONLY);
    if (handle < 0) throw std::runtime_error("Cannot flush render checkpoint");
    const int result = ::fsync(handle);
    ::close(handle);
    if (result) throw std::runtime_error("Cannot flush render checkpoint");
    handle = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (handle < 0) throw std::runtime_error("Cannot flush checkpoint directory");
    const int directory_result = ::fsync(handle);
    ::close(handle);
    if (directory_result) throw std::runtime_error("Cannot flush checkpoint directory");
#endif
}

struct Layout {
    RenderFilePlan public_plan;
    detail::RenderGeometry geometry;
    std::uint64_t base_bytes, chunk_samples, group_rows, groups;
};
Layout layout(const Flame& flame, const RenderOptions& opt, const FileRenderOptions& file) {
    if (flame.width > 16384 || flame.height > 16384)
        throw std::invalid_argument("File rendering supports dimensions up to 16384 by 16384");
    auto geometry = detail::render_geometry(flame, opt, false);
    if (opt.backend != RenderBackend::cpu && opt.backend != RenderBackend::automatic &&
        opt.backend != RenderBackend::vulkan)
        throw std::invalid_argument("Choose CPU, Automatic or Vulkan rendering");
    if (!opt.memory_mib || !file.disk_mib || !opt.gpu_memory_mib)
        throw std::invalid_argument("Render memory and disk budgets must be positive");
    if (file.resume && file.checkpoint_directory.empty())
        throw std::invalid_argument("Resume requires an explicit checkpoint directory");
    const auto budget = multiply(opt.memory_mib, MiB);
    // XML attributes may expand to six bytes per input byte. Charge the
    // serialization/checkpoint copies before constructing those strings.
    if (opt.device.size() > 65536)
        throw std::invalid_argument("Render device identifier is too long");
    std::uint64_t metadata = add(32768, add(multiply(add(flame.name.size(), flame.source_version.size()), 6),
                                           multiply(opt.device.size(), 4)));
    std::uint64_t model = multiply(multiply(flame.transforms.size(), flame.transforms.size()), 16);
    for (const auto& t : flame.transforms) {
        model = add(model, add(multiply(t.variations.size(), sizeof(Term) * 2), multiply(t.chaos.size(), 16)));
        metadata = add(metadata, add(1024, add(multiply(t.variations.size(), 128), multiply(t.chaos.size(), 64))));
        for (const auto& term : t.variations) if (term.id == Variation::plugin) {
            metadata = add(metadata, add(1024, multiply(term.plugin_parameters.size(), 256)));
            model = add(model, multiply(term.plugin_parameters.size(), sizeof(double) * 2));
        }
    }
    if (flame.final_transform) {
        metadata = add(metadata, add(1024, multiply(flame.final_transform->variations.size(), 128)));
        for (const auto& term : flame.final_transform->variations) if (term.id == Variation::plugin) {
            metadata = add(metadata, add(1024, multiply(term.plugin_parameters.size(), 256)));
            model = add(model, multiply(term.plugin_parameters.size(), sizeof(double) * 2));
        }
    }
    if (metadata > 2 * MiB)
        throw std::invalid_argument("Flame metadata exceeds the checkpointed file-rendering limit");
    model = add(model, multiply(metadata, 8));
    model = add(model, multiply(flame.transforms.size(), 4096));
    const auto row_bytes = multiply(flame.width, sizeof(std::array<double, 4>));
    const auto base = add(add(2 * MiB, model), add(row_bytes,
        ImageStreamWriter::working_bytes(flame.width, file.image)));
    const auto kernel_bytes = add(multiply(geometry.kernel.taps, sizeof(double)),
                                  ColorCurveEvaluator::storage_bytes(flame.curves));
    const auto fixed = add(base, kernel_bytes);
    const auto hist_row = multiply(geometry.gpu.width, sizeof(detail::Bin));
    const auto gutter_bytes = multiply(2ull * geometry.kernel.padding, hist_row);
    if (fixed > budget || gutter_bytes > budget - fixed)
        throw std::invalid_argument("Filter overlap and export buffers exceed the memory budget");
    const auto available_rows = (budget - fixed - gutter_bytes) / hist_row / unsigned(flame.oversample);
    if (!available_rows)
        throw std::invalid_argument("One reconstruction row exceeds the memory budget; reduce oversampling/filter or increase memory");
    const auto rows = file.strip_rows ? std::uint64_t(file.strip_rows) : std::min<std::uint64_t>(256, available_rows);
    if (!rows || rows > available_rows || rows > 16384)
        throw std::invalid_argument("Requested reconstruction strip exceeds the memory budget or dimension limit");
    const auto chunk_samples = std::min<std::uint64_t>(65536, (budget - fixed) / (4 * sizeof(Hit)) / batch_size * batch_size);
    if (!chunk_samples)
        throw std::invalid_argument("One sampling chunk exceeds the memory budget");
    const auto group_rows = multiply(rows, unsigned(flame.oversample));
    const auto groups = ceil_div(geometry.gpu.height, group_rows);
    if (groups > 1000000 || multiply(groups, 24) > MiB)
        throw std::invalid_argument("Filter/strip combination needs too much checkpoint indexing memory");
    const auto chunk_count = ceil_div(opt.samples, batch_size); // Conservative minimum chunk size.
    auto disk = add(multiply(opt.samples, sizeof(Hit)), multiply(chunk_count, add(64, multiply(groups, 24))));
    // Atomic updates retain the previous manifest while staging its replacement.
    // Quoting may double each serialized identity; reserve both copies as well
    // as fixed filesystem/checkpoint overhead before admitting the disk budget.
    const auto manifest_bytes = add(MiB, multiply(metadata, 4));
    disk = add(add(disk, image_export_upper_bound(flame.width, flame.height, file.image)), manifest_bytes);
    if (disk > multiply(file.disk_mib, MiB))
        throw std::invalid_argument("Conservative render working-storage estimate exceeds the disk budget");
    const auto reconstruction = add(fixed, multiply(add(group_rows, 2ull * geometry.kernel.padding), hist_row));
    const auto sampling = add(fixed, multiply(chunk_samples, 4 * sizeof(Hit)));
    return {{flame.width, flame.height, unsigned(rows), unsigned(ceil_div(flame.height, rows)),
             std::max(reconstruction, sampling), disk}, geometry, fixed, chunk_samples, group_rows, groups};
}

std::string identity(const Flame& f, const RenderOptions& o, const FileRenderOptions& file) {
    std::ostringstream s;
    s << "Apophysis-Reborn-stream-1\n" << __VERSION__ << '\n' << sizeof(void*) << ' ' << std::fegetround();
#ifdef _WIN32
    s << " Windows";
#else
    s << " POSIX";
#endif
    s << '\n' << o.samples << ' ' << o.seed << ' ' << int(o.backend) << ' ' << std::quoted(o.device)
      << ' ' << o.allow_cpu_fallback << ' ' << int(file.image.format) << ' ' << file.image.alpha
      << '\n' << serialize_flame(f);
    if (const auto plugins = plugin_identity(f); !plugins.empty()) s << "\nplugins\n" << plugins;
    for (const auto& t : f.transforms)
        s << '\n' << t.parameters.apophysis_math << ' ' << t.parameters.separate_blur_angle;
    if (f.final_transform)
        s << "\nfinal " << f.final_transform->parameters.apophysis_math << ' '
          << f.final_transform->parameters.separate_blur_angle;
    auto key = s.str();
    if (key.size() > 2 * MiB)
        throw std::invalid_argument("Render checkpoint identity exceeds the metadata limit");
    return key;
}
struct Manifest {
    std::string key, device = "CPU", backend_identity, fallback;
    std::uint64_t samples = 0, completed = 0, chunks = 0, plotted = 0, invalid = 0;
    std::uint32_t histogram_width = 0, histogram_height = 0;
    unsigned strip_rows = 0, group_rows = 0, workers = 0;
    RenderBackend backend = RenderBackend::cpu;
};
void save_manifest(const fs::path& directory, const Manifest& m) {
    const auto path = directory / "manifest.apocheckpoint";
    std::ostringstream body;
    body << "APO7_CHECKPOINT 1\n" << m.samples << ' ' << m.completed << ' ' << m.chunks << ' '
            << m.plotted << ' ' << m.invalid << ' ' << m.histogram_width << ' ' << m.histogram_height
            << ' ' << m.strip_rows << ' ' << m.group_rows << ' ' << m.workers << ' ' << int(m.backend)
            << '\n' << std::quoted(m.device) << '\n' << std::quoted(m.backend_identity) << '\n'
            << std::quoted(m.fallback) << '\n' << std::quoted(m.key) << "\nEND\n";
    const auto bytes = body.str();
    write_atomic(path, [&](std::ostream& out) {
        out << bytes << "HASH " << std::hex << hash_bytes(hash_start, bytes.data(), bytes.size()) << '\n';
    });
    sync_file(path);
}
Manifest read_manifest(const fs::path& directory) {
    const auto path = directory / "manifest.apocheckpoint";
    if (fs::file_size(path) > 16 * MiB) throw std::runtime_error("Render checkpoint manifest is too large");
    std::ifstream source(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(source)), {});
    const auto marker = bytes.rfind("\nHASH ");
    if (marker == std::string::npos || bytes.empty() || bytes.back() != '\n')
        throw std::runtime_error("Render checkpoint manifest checksum is missing");
    const auto digest = std::string_view(bytes).substr(marker + 6, bytes.size() - marker - 7);
    std::uint64_t expected = 0;
    const auto parsed = std::from_chars(digest.data(), digest.data() + digest.size(), expected, 16);
    if (digest.empty() || parsed.ec != std::errc{} || parsed.ptr != digest.data() + digest.size() ||
        hash_bytes(hash_start, bytes.data(), marker + 1) != expected)
        throw std::runtime_error("Render checkpoint manifest checksum mismatch");
    std::istringstream in(bytes.substr(0, marker + 1));
    Manifest m;
    std::string magic, end;
    int version = 0, backend = 0;
    if (!(in >> magic >> version) || magic != "APO7_CHECKPOINT" || version != 1 ||
        !(in >> m.samples >> m.completed >> m.chunks >> m.plotted >> m.invalid >> m.histogram_width
             >> m.histogram_height >> m.strip_rows >> m.group_rows >> m.workers >> backend
             >> std::quoted(m.device) >> std::quoted(m.backend_identity) >> std::quoted(m.fallback)
             >> std::quoted(m.key) >> end) || end != "END" || !m.samples ||
        m.samples > UINT64_MAX / 16777215 || m.completed > m.samples ||
        m.plotted > m.completed || m.chunks > ceil_div(m.samples, 256) ||
        !m.histogram_width || !m.histogram_height || !m.strip_rows || m.strip_rows > 16384 || !m.group_rows ||
        (backend != int(RenderBackend::cpu) && backend != int(RenderBackend::vulkan)) ||
        m.device.size() > 65536 || m.backend_identity.size() > 65536 || m.fallback.size() > 65536 ||
        m.key.size() > 8 * MiB || m.key.empty() || (m.completed != 0 && m.chunks == 0))
        throw std::runtime_error("Invalid render checkpoint manifest");
    in >> std::ws;
    if (!in.eof()) throw std::runtime_error("Unexpected data in checkpoint manifest");
    m.backend = RenderBackend(backend);
    return m;
}
fs::path part_path(const fs::path& directory, std::uint64_t index) {
    return directory / ("chunk-" + std::to_string(index) + ".apohits");
}
struct Group { std::uint64_t offset, count, hash; };
struct Chunk {
    std::uint64_t start, samples, plotted, invalid;
    std::vector<Group> groups;
};
Chunk read_chunk_header(std::ifstream& in, const fs::path& path, const Manifest& m) {
    const auto size = fs::file_size(path);
    std::array<char, 8> magic{};
    if (!in.read(magic.data(), 8) || std::string_view(magic.data(), 8) != std::string_view("APO7HIT\0", 8))
        throw std::runtime_error("Invalid render checkpoint chunk signature");
    Chunk c{get64(in), get64(in), get64(in), get64(in), {}};
    const auto groups = get64(in), bytes = get64(in), reserved = get64(in);
    if (!c.samples || c.samples > 65536 || c.start > m.samples || c.samples > m.samples - c.start ||
        c.plotted > c.samples || c.invalid > c.samples + 32 * ceil_div(c.samples, 256) ||
        groups != ceil_div(m.histogram_height, m.group_rows) || groups > 1000000 ||
        multiply(groups, 24) > MiB || bytes != multiply(c.plotted, 24) || reserved ||
        size != add(add(64, multiply(groups, 24)), bytes))
        throw std::runtime_error("Invalid render checkpoint chunk size or counts");
    c.groups.reserve(static_cast<std::size_t>(groups));
    std::uint64_t offset = 64 + groups * 24, total = 0;
    for (std::uint64_t i = 0; i < groups; ++i) {
        Group g{get64(in), get64(in), get64(in)};
        if (g.offset != offset || g.count > c.plotted - total)
            throw std::runtime_error("Invalid checkpoint group index");
        offset += g.count * 24;
        total += g.count;
        c.groups.push_back(g);
    }
    if (total != c.plotted || offset != size) throw std::runtime_error("Incomplete checkpoint group index");
    return c;
}
template<class Visitor>
void read_group(std::ifstream& in, const Group& group, unsigned group_index, const Manifest& m,
                std::stop_token stop, const Visitor& visit) {
    in.seekg(static_cast<std::streamoff>(group.offset));
    std::uint64_t hash = hash_start;
    std::array<char, 24 * 1024> bytes;
    for (std::uint64_t first = 0; first < group.count;) {
        cancel(stop);
        const auto n = std::min<std::uint64_t>(1024, group.count - first);
        if (!in.read(bytes.data(), static_cast<std::streamsize>(n * 24)))
            throw std::runtime_error("Truncated render checkpoint hit data");
        hash = hash_bytes(hash, bytes.data(), static_cast<std::size_t>(n * 24));
        for (std::uint64_t i = 0; i < n; ++i) {
            const auto hit = decode(bytes.data() + i * 24);
            if (hit[0] >= m.histogram_width || hit[1] >= m.histogram_height ||
                hit[1] / m.group_rows != group_index || hit[5] > 16777215 ||
                hit[2] > hit[5] || hit[3] > hit[5] || hit[4] > hit[5])
                throw std::runtime_error("Invalid checkpoint sample coordinates or channel values");
            visit(hit);
        }
        first += n;
    }
    if (hash != group.hash) throw std::runtime_error("Render checkpoint checksum mismatch");
}
void verify_checkpoint(const fs::path& directory, const Manifest& m, bool payload, std::stop_token stop) {
    std::uint64_t completed = 0, plotted = 0, invalid = 0;
    for (std::uint64_t i = 0; i < m.chunks; ++i) {
        cancel(stop);
        const auto path = part_path(directory, i);
        std::ifstream in(path, std::ios::binary);
        auto c = read_chunk_header(in, path, m);
        if (c.start != completed || (c.start % (m.backend == RenderBackend::cpu ? batch_size : 256)))
            throw std::runtime_error("Noncontiguous render checkpoint sampling range");
        if (payload)
            for (unsigned g = 0; g < c.groups.size(); ++g)
                read_group(in, c.groups[g], g, m, stop, [](const Hit&) {});
        completed += c.samples;
        plotted += c.plotted;
        invalid += c.invalid;
    }
    if (completed != m.completed || plotted != m.plotted || invalid != m.invalid ||
        (completed != m.samples && completed % (m.backend == RenderBackend::cpu ? batch_size : 256)))
        throw std::runtime_error("Render checkpoint totals do not match its chunks");
}

void write_chunk(const fs::path& directory, const Manifest& m, std::uint64_t n,
                 std::uint64_t invalid, std::vector<Hit>& hits, std::stop_token stop) {
    cancel(stop);
    std::sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) { return a[1] / m.group_rows < b[1] / m.group_rows; });
    const auto group_count = ceil_div(m.histogram_height, m.group_rows);
    std::vector<Group> groups(static_cast<std::size_t>(group_count), {0, 0, hash_start});
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if ((i & 4095u) == 0) cancel(stop);
        const auto& hit = hits[i];
        if (hit[0] >= m.histogram_width || hit[1] >= m.histogram_height)
            throw std::runtime_error("Sampler returned a hit outside the global histogram");
        auto& group = groups[hit[1] / m.group_rows];
        ++group.count;
        const auto bytes = encode(hit);
        group.hash = hash_bytes(group.hash, bytes.data(), bytes.size());
    }
    auto offset = 64 + group_count * 24;
    for (auto& group : groups) { group.offset = offset; offset += group.count * 24; }
    const auto path = part_path(directory, m.chunks);
    write_atomic(path, [&](std::ostream& out) {
        out.write("APO7HIT\0", 8);
        for (const auto value : {m.completed, n, std::uint64_t(hits.size()), invalid, group_count,
                                 std::uint64_t(hits.size()) * 24, UINT64_C(0)}) put64(out, value);
        for (const auto& group : groups) { put64(out, group.offset); put64(out, group.count); put64(out, group.hash); }
        for (std::size_t i = 0; i < hits.size(); ++i) {
            if ((i & 4095u) == 0) cancel(stop);
            const auto bytes = encode(hits[i]);
            out.write(bytes.data(), bytes.size());
        }
    }, [&] { cancel(stop); });
    sync_file(path);
}

fs::path temporary_directory(const fs::path& output) {
    const auto parent = output.has_parent_path() ? output.parent_path() : fs::current_path();
    const auto tick = Clock::now().time_since_epoch().count();
    for (unsigned i = 0; i < 100; ++i) {
        auto path = parent / (".apophysis-render-" + std::to_string(tick) + "-" + std::to_string(i));
        if (fs::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot reserve render working storage");
}
} // namespace

RenderFilePlan plan_file_render(const Flame& f, const RenderOptions& o, const FileRenderOptions& file) {
    return layout(f, o, file).public_plan;
}
bool has_render_checkpoint(const fs::path& directory) noexcept {
    try {
        const auto m = read_manifest(directory);
        verify_checkpoint(directory, m, false, {});
        return true;
    } catch (...) { return false; }
}

FileRenderResult render_to_file(const Flame& f, const RenderOptions& original,
                               const fs::path& output, const FileRenderOptions& supplied) {
    gpu::ContextScope contextScope;
    auto file = supplied;
    RenderOptions opt = original;
    const auto started = Clock::now();
    cancel(opt.stop_token);
    if (output.empty()) throw std::invalid_argument("Choose an output file");
    auto l = layout(f, opt, file);
    const bool temporary = file.checkpoint_directory.empty();
    const auto directory = temporary ? temporary_directory(output) : fs::absolute(file.checkpoint_directory);
    struct Cleanup { fs::path path; bool remove; ~Cleanup() { if (remove) { std::error_code e; fs::remove_all(path, e); } } } cleanup{directory, temporary};
    if (!temporary) {
        if (!fs::exists(directory)) {
            if (file.resume) throw std::runtime_error("Render checkpoint directory does not exist");
            fs::create_directories(directory);
        } else if (!file.resume && !fs::is_empty(directory)) {
            throw std::runtime_error("Checkpoint directory already contains data; resume it or choose a new directory");
        }
    }
    CheckpointLock lock(directory / "checkpoint.lock");
    const auto canonical_directory = fs::weakly_canonical(directory);
    for (auto parent = fs::weakly_canonical(output); !parent.empty();) {
        if (parent == canonical_directory)
            throw std::invalid_argument("Output must be outside the render checkpoint directory");
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    Manifest m;
    const auto key = identity(f, opt, file);
    if (file.resume) {
        m = read_manifest(directory);
        if (m.key != key) throw std::runtime_error("Checkpoint does not match the flame, sampling, backend, export settings or engine build");
        if (file.strip_rows && file.strip_rows != m.strip_rows)
            throw std::runtime_error("Checkpoint uses a different reconstruction strip height");
        file.strip_rows = m.strip_rows;
        l = layout(f, opt, file);
        if (m.samples != opt.samples || m.histogram_width != l.geometry.gpu.width ||
            m.histogram_height != l.geometry.gpu.height || m.group_rows != l.group_rows)
            throw std::runtime_error("Checkpoint geometry is inconsistent");
        verify_checkpoint(directory, m, true, opt.stop_token);
    } else {
        m.key = key;
        m.samples = opt.samples;
        m.histogram_width = l.geometry.gpu.width;
        m.histogram_height = l.geometry.gpu.height;
        m.strip_rows = l.public_plan.strip_rows;
        m.group_rows = unsigned(l.group_rows);
        m.backend = opt.backend == RenderBackend::cpu ? RenderBackend::cpu : RenderBackend::vulkan;
        m.backend_identity = m.backend == RenderBackend::cpu ? "CPU MT19937-64 / stream-1" : "";
        save_manifest(directory, m);
    }
    if (!color_curves_are_identity(f.curves))
        l.geometry.curves = std::make_shared<ColorCurveEvaluator>(f.curves);
    const auto output_parent = output.has_parent_path() ? output.parent_path() : fs::current_path();
    const auto output_bytes = image_export_upper_bound(f.width, f.height, file.image);
    std::error_code ec;
    const auto work_space = fs::space(directory, ec);
    std::uint64_t committed_bytes = 0;
    for (std::uint64_t i = 0; i < m.chunks; ++i)
        committed_bytes = add(committed_bytes, fs::file_size(part_path(directory, i)));
    const auto remaining_disk = l.public_plan.disk_bytes - std::min(l.public_plan.disk_bytes, committed_bytes);
    if (!ec && work_space.available < remaining_disk)
        throw std::runtime_error("Insufficient free space for the conservative remaining render-storage estimate");
    const auto output_space = fs::space(output_parent, ec);
    if (!ec && output_space.available < output_bytes)
        throw std::runtime_error("Insufficient free space for the selected output format");
    const auto report = [&](std::string phase, unsigned strips = 0) {
        if (file.status) file.status({std::move(phase), m.completed, opt.samples, strips,
                                     l.public_plan.strip_count,
                                     std::chrono::duration<double>(Clock::now() - started).count(),
                                     m.backend_identity.empty() ? std::string{} : std::string(backend_name(m.backend)),
                                     m.backend_identity.empty() ? std::string{} : m.device, m.fallback});
    };
    if (opt.progress) opt.progress(m.completed, opt.samples);
    report("Sampling");
    while (m.completed < opt.samples) {
        cancel(opt.stop_token);
        const auto n = std::min(l.chunk_samples, opt.samples - m.completed);
        std::vector<Hit> hits;
        std::uint64_t invalid = 0;
        if (m.backend == RenderBackend::vulkan) {
            try {
                if (has_plugin_variations(f))
                    throw gpu::Unavailable("Native plugin variations require the CPU renderer");
                auto inner = opt;
                inner.progress = {};
                inner.backend = original.backend;
                auto sampled = gpu::sample_hits(f, inner, l.geometry.gpu, m.completed / 256,
                                               unsigned(ceil_div(n, 256)), l.base_bytes);
                if (!m.backend_identity.empty() && m.backend_identity != sampled.checkpoint_identity)
                    throw std::runtime_error("Checkpoint GPU, driver or shader identity changed; start a fresh render");
                m.backend_identity = std::move(sampled.checkpoint_identity);
                m.device = std::move(sampled.device_name);
                hits = std::move(sampled.hits);
                invalid = sampled.invalid;
            } catch (const gpu::Unavailable& error) {
                if (!opt.allow_cpu_fallback || m.completed)
                    throw std::runtime_error(std::string("Vulkan streaming render: ") + error.what());
                m.backend = RenderBackend::cpu;
                m.device = "CPU";
                m.backend_identity = "CPU MT19937-64 / stream-1";
                m.fallback = error.what();
            }
        }
        if (m.backend == RenderBackend::cpu) {
            auto sampled = detail::sample_cpu_hits(f, opt, l.geometry, m.completed / batch_size,
                                                   unsigned(ceil_div(n, batch_size)));
            hits = std::move(sampled.hits);
            invalid = sampled.invalid;
            m.workers = std::max(m.workers, sampled.workers);
        }
        cancel(opt.stop_token);
        write_chunk(directory, m, n, invalid, hits, opt.stop_token);
        m.completed += n;
        ++m.chunks;
        m.plotted += hits.size();
        m.invalid += invalid;
        save_manifest(directory, m); // Publish only after the immutable chunk is closed and flushed.
        if (opt.progress) opt.progress(m.completed, opt.samples);
        report("Sampling");
    }
    cancel(opt.stop_token);
    const auto sampled_at = Clock::now();
    const auto stride = std::size_t(l.geometry.gpu.width);
    report("Reconstructing");
    write_atomic(output, [&](std::ostream& out) {
        ImageStreamWriter writer(out, f.width, f.height, file.image);
        std::vector<std::array<double, 4>> row(static_cast<std::size_t>(f.width));
        for (unsigned strip = 0; strip < l.public_plan.strip_count; ++strip) {
            cancel(opt.stop_token);
            const auto y = strip * l.public_plan.strip_rows;
            const auto rows = std::min(l.public_plan.strip_rows, unsigned(f.height) - y);
            const auto first = y * unsigned(f.oversample);
            const auto height = rows * unsigned(f.oversample) + 2u * l.geometry.kernel.padding;
            std::vector<detail::Bin> bins(std::size_t(height) * stride);
            const auto first_group = first / m.group_rows;
            const auto last_group = (std::uint64_t(first) + height - 1) / m.group_rows;
            for (std::uint64_t part = 0; part < m.chunks; ++part) {
                cancel(opt.stop_token);
                const auto path = part_path(directory, part);
                std::ifstream in(path, std::ios::binary);
                const auto chunk = read_chunk_header(in, path, m);
                for (auto g = first_group; g <= last_group; ++g) {
                    read_group(in, chunk.groups.at(g), unsigned(g), m, opt.stop_token, [&](const Hit& hit) {
                        if (hit[1] < first || std::uint64_t(hit[1]) >= std::uint64_t(first) + height) return;
                        auto& bin = bins[(std::size_t(hit[1]) - first) * stride + hit[0]];
                        for (unsigned c = 0; c < 4; ++c) bin[c] += hit[c + 2];
                    });
                }
            }
            for (unsigned i = 0; i < rows; ++i) {
                detail::develop_row(f, l.geometry, bins, first, int(y + i), opt.samples, row,
                                    image_format_bits(file.image.format) == 16, file.image.alpha,
                                    opt.stop_token);
                writer.write_row(row);
            }
            report("Reconstructing", strip + 1);
        }
        cancel(opt.stop_token);
        writer.finish();
    }, [&] {
        cancel(opt.stop_token);
        if (file.before_publish) file.before_publish();
    });
    report("Complete", l.public_plan.strip_count);
    return {m.plotted, m.invalid, m.workers, m.backend, m.device, m.fallback,
            std::chrono::duration<double, std::milli>(sampled_at - started).count(),
            std::chrono::duration<double, std::milli>(Clock::now() - sampled_at).count(), supplied.resume};
}
} // namespace apo
