#include "plugin.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <expat.h>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace apo {
namespace fs = std::filesystem;
namespace {
constexpr std::uint64_t max_library_bytes = 64ull * 1024 * 1024;
constexpr std::size_t max_parameters = 64;

using detail::Sha256;
std::string digest_file(const fs::path &path) {
    if (!fs::is_regular_file(path) || fs::is_symlink(path))
        throw std::runtime_error("Plugin library must be a regular, non-symlink file");
    const auto size = fs::file_size(path);
    if (!size || size > max_library_bytes)
        throw std::runtime_error("Plugin library size is outside 1 byte through 64 MiB");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read plugin library");
    Sha256 hash;
    std::array<unsigned char, 65536> bytes{};
    std::uint64_t read = 0;
    while (in) {
        in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        const auto n = in.gcount();
        if (n > 0) {
            hash.add(bytes.data(), static_cast<std::size_t>(n));
            read += n;
        }
    }
    if (!in.eof() || read != size)
        throw std::runtime_error("Plugin library changed or failed while reading");
    return hash.finish();
}
bool identifier(std::string_view name, std::size_t maximum = 64, bool dots = false) {
    if (name.empty() || name.size() > maximum || name.front() < 'a' || name.front() > 'z')
        return false;
    return std::all_of(name.begin(), name.end(), [&](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
               (dots && (c == '.' || c == '-'));
    });
}
void bounded_text(const std::string &text, std::size_t max, const char *field) {
    if (text.empty() || text.size() > max || text.find_first_of("\r\n\t") != std::string::npos)
        throw std::invalid_argument(std::string("Invalid plugin ") + field);
}
double numeric(std::string_view s) {
    double d{};
    const auto r = std::from_chars(s.data(), s.data() + s.size(), d);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !std::isfinite(d))
        throw std::invalid_argument("Invalid plugin parameter number");
    return d;
}
void parameter_valid(const PluginParameter &p) {
    if (!identifier(p.name) || !std::isfinite(p.minimum) || !std::isfinite(p.maximum) ||
        !std::isfinite(p.default_value) || p.minimum > p.maximum || p.default_value < p.minimum ||
        p.default_value > p.maximum ||
        (p.integer && (std::trunc(p.minimum) != p.minimum || std::trunc(p.maximum) != p.maximum ||
                       std::trunc(p.default_value) != p.default_value)))
        throw std::invalid_argument("Invalid plugin parameter metadata: " + p.name);
}
bool reserved_name(std::string_view name) {
    static constexpr std::array names{
        "weight",       "color",       "opacity", "plotmode",  "name",  "enabled",
        "julian_power", "julian_dist", "flatten", "var_order", "coefs", "post",
        "color_speed",  "symmetry",    "chaos",   "var",       "var1"};
    return find_variation(name).has_value() ||
           std::find(names.begin(), names.end(), name) != names.end();
}
bool same_descriptor(const PluginInfo &a, const PluginInfo &b) {
    return a.id == b.id && a.name == b.name && a.version == b.version &&
           a.description == b.description && a.parameters == b.parameters &&
           a.fingerprint == b.fingerprint;
}
std::set<std::string> serialized_fields(const PluginInfo &info) {
    std::set<std::string> fields{info.name};
    for (const auto &p : info.parameters)
        fields.insert(info.name + "_" + p.name);
    return fields;
}
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<const PluginVariation>, std::less<>> enabled;
    std::map<fs::path, std::weak_ptr<const PluginVariation>> live;
};
Registry &registry() {
    static Registry r;
    return r;
}

PluginInfo read_manifest(const fs::path &path) {
    if (path.extension() != ".apoplugin" || !fs::is_regular_file(path) || fs::is_symlink(path))
        throw std::runtime_error("Choose a regular .apoplugin manifest");
    if (fs::file_size(path) > 65536)
        throw std::runtime_error("Plugin manifest exceeds 64 KiB");
    std::ifstream input(path, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input), {}};
    if (!input.eof() && input.fail())
        throw std::runtime_error("Cannot read plugin manifest");
    struct State {
        PluginInfo info;
        XML_Parser parser = nullptr;
        int depth = 0;
        bool root = false;
        std::string library, error;
    } s;
    s.info.manifest_path = fs::canonical(path);
    s.parser = XML_ParserCreate(nullptr);
    if (!s.parser)
        throw std::bad_alloc();
    const auto owner =
        std::unique_ptr<std::remove_pointer_t<XML_Parser>, decltype(&XML_ParserFree)>(
            s.parser, XML_ParserFree);
    XML_SetUserData(s.parser, &s);
    XML_SetStartElementHandler(s.parser, [](void *data, const char *tag, const char **attrs) {
        auto &s = *static_cast<State *>(data);
        try {
            std::map<std::string, std::string> a;
            for (; *attrs; attrs += 2)
                a.emplace(attrs[0], attrs[1]);
            auto take = [&](const char *key) {
                auto it = a.find(key);
                if (it == a.end())
                    throw std::runtime_error(std::string("Missing plugin attribute ") + key);
                auto value = it->second;
                a.erase(it);
                return value;
            };
            if (s.depth == 0 && std::string_view(tag) == "apophysis-plugin" && !s.root) {
                s.root = true;
                if (take("abi") != "1")
                    throw std::runtime_error("Unsupported plugin ABI; expected 1");
                s.info.id = take("id");
                s.info.name = take("name");
                s.info.version = take("version");
                s.info.description = take("description");
                s.library = take("library");
            } else if (s.depth == 1 && std::string_view(tag) == "parameter") {
                PluginParameter p;
                p.name = take("name");
                p.default_value = numeric(take("default"));
                p.minimum = numeric(take("min"));
                p.maximum = numeric(take("max"));
                const auto type = take("type");
                if (type != "real" && type != "integer")
                    throw std::runtime_error("Plugin parameter type must be real or integer");
                p.integer = type == "integer";
                parameter_valid(p);
                if (s.info.parameters.size() >= max_parameters)
                    throw std::runtime_error("Plugin has more than 64 parameters");
                for (const auto &q : s.info.parameters)
                    if (q.name == p.name)
                        throw std::runtime_error("Duplicate plugin parameter " + p.name);
                s.info.parameters.push_back(std::move(p));
            } else
                throw std::runtime_error("Unsupported plugin manifest element");
            if (!a.empty())
                throw std::runtime_error("Unknown plugin manifest attribute: " + a.begin()->first);
            ++s.depth;
        } catch (const std::exception &e) {
            s.error = e.what();
            XML_StopParser(s.parser, XML_FALSE);
        }
    });
    XML_SetEndElementHandler(s.parser,
                             [](void *data, const char *) { --static_cast<State *>(data)->depth; });
    XML_SetCharacterDataHandler(s.parser, [](void *data, const char *text, int count) {
        auto &s = *static_cast<State *>(data);
        for (int i = 0; i < count; ++i)
            if (text[i] != ' ' && text[i] != '\n' && text[i] != '\r' && text[i] != '\t') {
                s.error = "Plugin manifest cannot contain text payloads";
                XML_StopParser(s.parser, XML_FALSE);
                break;
            }
    });
    XML_SetStartDoctypeDeclHandler(s.parser,
                                   [](void *data, const char *, const char *, const char *, int) {
                                       auto &s = *static_cast<State *>(data);
                                       s.error = "Plugin manifest DTDs are not allowed";
                                       XML_StopParser(s.parser, XML_FALSE);
                                   });
    if (XML_Parse(s.parser, bytes.data(), static_cast<int>(bytes.size()), XML_TRUE) ==
        XML_STATUS_ERROR)
        throw std::runtime_error(s.error.empty() ? std::string("Invalid plugin XML: ") +
                                                       XML_ErrorString(XML_GetErrorCode(s.parser))
                                                 : s.error);
    if (!s.root)
        throw std::runtime_error("Missing plugin manifest root");
    if (!identifier(s.info.id, 128, true) || !identifier(s.info.name) || reserved_name(s.info.name))
        throw std::runtime_error("Invalid or reserved plugin identity/variation name");
    if (s.info.name.starts_with("pre_") || s.info.name.starts_with("post_") ||
        s.info.name.starts_with("dc_") || s.info.name.starts_with("reborn_"))
        throw std::runtime_error(
            "Plugin ABI v1 supports ordinary 2D variations, not pre/post/direct-color stages");
    bounded_text(s.info.version, 64, "version");
    bounded_text(s.info.description, 2048, "description");
    const fs::path library{std::u8string(s.library.begin(), s.library.end())};
    if (library.empty() || library.is_absolute() || library.has_parent_path() ||
        s.library.find_first_of("/\\:") != std::string::npos || library.filename() == "." ||
        library.filename() == "..")
        throw std::runtime_error(
            "Plugin library must be one relative filename beside the manifest");
    s.info.library_path = s.info.manifest_path.parent_path() / library;
    if (fs::is_symlink(s.info.library_path))
        throw std::runtime_error("Plugin library symlinks are not accepted");
    if (fs::canonical(s.info.library_path).parent_path() != s.info.manifest_path.parent_path())
        throw std::runtime_error("Plugin library is outside its package directory");
    s.info.fingerprint = digest_file(s.info.library_path);
    return s.info;
}
void verify_machine(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::array<unsigned char, 64> b{};
    in.read(reinterpret_cast<char *>(b.data()), b.size());
    if (in.gcount() < 64)
        throw std::runtime_error("Plugin library has no supported executable header");
    const auto le32 = [&](unsigned p) {
        return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) |
               (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24);
    };
    if (b[0] == 'M' && b[1] == 'Z') {
        const auto offset = le32(60);
        if (offset > fs::file_size(path) - 6)
            throw std::runtime_error("Truncated PE plugin header");
        in.seekg(offset);
        std::array<unsigned char, 6> pe{};
        in.read(reinterpret_cast<char *>(pe.data()), pe.size());
        if (in.gcount() != 6 || pe[0] != 'P' || pe[1] != 'E' || pe[2] || pe[3])
            throw std::runtime_error("Invalid PE plugin header");
        const auto machine = unsigned(pe[4]) | (unsigned(pe[5]) << 8);
        if (machine == 0x14c && sizeof(void *) == 8)
            throw std::runtime_error("A 32-bit legacy DLL cannot load into this 64-bit process; "
                                     "port its source using the Reborn SDK");
#ifdef _WIN32
#if defined(_M_X64) || defined(__x86_64__)
        if (machine != 0x8664)
            throw std::runtime_error("Plugin architecture differs from the x64 application");
#endif
        return;
#else
        throw std::runtime_error("A Windows DLL cannot execute in this native platform; build its "
                                 "source for this platform");
#endif
    }
#ifndef _WIN32
    if (b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F') {
        if (b[4] != (sizeof(void *) == 8 ? 2 : 1) || b[5] != 1)
            throw std::runtime_error(
                "Plugin ELF bitness or byte order differs from this application");
#if defined(__x86_64__)
        if ((unsigned(b[18]) | (unsigned(b[19]) << 8)) != 62)
            throw std::runtime_error("Plugin architecture differs from the x64 application");
#endif
        return;
    }
#endif
    throw std::runtime_error("Unsupported plugin executable format for this platform");
}
std::string api_text(const char *value, std::size_t maximum, const char *field) {
    if (!value)
        throw std::runtime_error(std::string("Null plugin ") + field);
    for (std::size_t length = 0; length <= maximum; ++length)
        if (!value[length])
            return {value, length};
    throw std::runtime_error(std::string("Overlong plugin ") + field);
}
double APO_PLUGIN_CALL random_value(void *context) { return static_cast<Rng *>(context)->unit(); }
} // namespace

struct PluginVariation::Module {
#ifdef _WIN32
    HMODULE handle = nullptr;
    ~Module() {
        if (handle)
            FreeLibrary(handle);
    }
#else
    void *handle = nullptr;
    ~Module() {
        if (handle)
            dlclose(handle);
    }
#endif
};

PluginInfo inspect_plugin(const fs::path &manifest) {
    auto info = read_manifest(manifest);
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.enabled.find(info.name);
    info.enabled = it != r.enabled.end() &&
                   it->second->info().manifest_path == info.manifest_path &&
                   same_descriptor(it->second->info(), info);
    return info;
}
std::vector<PluginDiscovery> discover_plugins(const std::vector<fs::path> &paths) {
    std::vector<PluginDiscovery> result;
    std::set<fs::path> seen;
    const auto add = [&](const fs::path &p) {
        if (result.size() >= 1024)
            throw std::runtime_error("Plugin discovery exceeds 1024 manifests");
        std::error_code e;
        const auto canonical = fs::weakly_canonical(p, e);
        if (e || !seen.insert(canonical).second)
            return;
        PluginDiscovery item;
        item.path = p;
        try {
            item.info = inspect_plugin(p);
        } catch (const std::exception &x) {
            item.error = x.what();
        }
        result.push_back(std::move(item));
    };
    for (const auto &path : paths) {
        std::error_code error;
        if (fs::is_regular_file(path, error)) {
            add(path);
            continue;
        }
        if (!fs::is_directory(path, error)) {
            result.push_back({path, {}, "Plugin search directory is unavailable"});
            continue;
        }
        for (const auto &entry : fs::directory_iterator(path)) {
            if (entry.is_symlink())
                continue;
            if (entry.is_regular_file() && entry.path().extension() == ".apoplugin")
                add(entry.path());
            else if (entry.is_directory())
                for (const auto &nested : fs::directory_iterator(entry.path()))
                    if (!nested.is_symlink() && nested.is_regular_file() &&
                        nested.path().extension() == ".apoplugin")
                        add(nested.path());
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.path < b.path; });
    return result;
}
PluginInfo enable_plugin(const fs::path &manifest, std::string_view expected) {
    auto info = read_manifest(manifest);
    if (!expected.empty() && info.fingerprint != expected)
        throw std::runtime_error(
            "Plugin binary changed since approval; inspect and explicitly approve it again");
    verify_machine(info.library_path);
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    if (auto it = r.enabled.find(info.name); it != r.enabled.end()) {
        const auto &existing = it->second->info();
        if (existing.manifest_path == info.manifest_path && same_descriptor(existing, info))
            return existing;
        throw std::runtime_error("Another enabled plugin already owns variation " + info.name);
    }
    const auto fields = serialized_fields(info);
    for (const auto &[name, active] : r.enabled) {
        if (active->info().id == info.id)
            throw std::runtime_error("Another enabled plugin already owns ID " + info.id);
        for (const auto &field : serialized_fields(active->info()))
            if (fields.contains(field))
                throw std::runtime_error("Plugin XML field collides with enabled variation " +
                                         name + ": " + field);
    }
    if (auto existing = r.live[info.library_path].lock()) {
        if (!same_descriptor(existing->info(), info))
            throw std::runtime_error("A live render retains a different module at this path; "
                                     "restart the application before loading its replacement");
        r.enabled.emplace(info.name, existing);
        info.enabled = true;
        return info;
    }
    auto variation = std::make_shared<PluginVariation>();
    variation->module_ = std::make_shared<PluginVariation::Module>();
    ApoPluginQuery query = nullptr;
#ifdef _WIN32
    variation->module_->handle =
        LoadLibraryExW(info.library_path.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!variation->module_->handle)
        throw std::runtime_error("Cannot load plugin DLL (Windows error " +
                                 std::to_string(GetLastError()) + ")");
    query = reinterpret_cast<ApoPluginQuery>(
        GetProcAddress(variation->module_->handle, "apo_plugin_query"));
#else
    variation->module_->handle = dlopen(info.library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!variation->module_->handle)
        throw std::runtime_error(std::string("Cannot load plugin: ") + dlerror());
    query = reinterpret_cast<ApoPluginQuery>(dlsym(variation->module_->handle, "apo_plugin_query"));
#endif
    if (!query)
        throw std::runtime_error("Module does not export the Reborn apo_plugin_query ABI; legacy "
                                 "PluginVar DLLs require a source port");
    const ApoPluginV1 *api = nullptr;
    try {
        api = query(APO_PLUGIN_ABI_V1);
    } catch (...) {
        throw std::runtime_error("Plugin metadata callback threw an exception");
    }
    if (!api || api->struct_size != sizeof(ApoPluginV1) || api->abi_version != APO_PLUGIN_ABI_V1 ||
        !api->evaluate || api->parameter_count != info.parameters.size() ||
        (!api->parameters && api->parameter_count))
        throw std::runtime_error("Plugin ABI, descriptor size or parameter count is incompatible");
    if (api_text(api->id, 128, "id") != info.id || api_text(api->name, 64, "name") != info.name ||
        api_text(api->version, 64, "version") != info.version ||
        api_text(api->description, 2048, "description") != info.description)
        throw std::runtime_error("Plugin descriptor does not match its manifest metadata");
    for (std::size_t i = 0; i < info.parameters.size(); ++i) {
        const auto &p = api->parameters[i];
        if (p.kind > APO_PLUGIN_PARAMETER_INTEGER)
            throw std::runtime_error("Unsupported native plugin parameter type");
        PluginParameter actual{api_text(p.name, 64, "parameter name"), p.default_value, p.minimum,
                               p.maximum, p.kind == APO_PLUGIN_PARAMETER_INTEGER};
        parameter_valid(actual);
        if (actual != info.parameters[i])
            throw std::runtime_error("Native plugin parameter metadata differs from manifest");
    }
    if (digest_file(info.library_path) != info.fingerprint)
        throw std::runtime_error("Plugin binary changed while loading");
    info.enabled = true;
    variation->info_ = info;
    variation->api_ = api;
    r.enabled.emplace(info.name, variation);
    r.live[info.library_path] = variation;
    return info;
}
bool disable_plugin(std::string_view name) {
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    return r.enabled.erase(std::string(name)) != 0;
}
std::vector<PluginInfo> enabled_plugins() {
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<PluginInfo> result;
    for (const auto &[name, item] : r.enabled)
        result.push_back(item->info());
    return result;
}
std::shared_ptr<const PluginVariation> find_plugin_variation(std::string_view name) {
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.enabled.find(name);
    return it == r.enabled.end() ? nullptr : it->second;
}

PluginInfo install_plugin(const fs::path &manifest, const fs::path &destination) {
    const auto source = inspect_plugin(manifest);
    fs::create_directories(destination);
    const auto target = fs::absolute(destination) / source.name;
    if (fs::exists(target))
        throw std::runtime_error("Plugin installation already exists; no files were overwritten");
    fs::path temporary;
    for (unsigned i = 0; i < 1000; ++i) {
        auto p = fs::absolute(destination) / (".install-" + source.name + "-" + std::to_string(i));
        if (fs::create_directory(p)) {
            temporary = p;
            break;
        }
    }
    if (temporary.empty())
        throw std::runtime_error("Cannot reserve plugin installation directory");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code e;
            if (!path.empty())
                fs::remove_all(path, e);
        }
    } cleanup{temporary};
    fs::copy_file(source.library_path, temporary / source.library_path.filename());
    fs::copy_file(source.manifest_path, temporary / source.manifest_path.filename());
    const auto copy = inspect_plugin(temporary / source.manifest_path.filename());
    if (copy.fingerprint != source.fingerprint)
        throw std::runtime_error("Plugin changed during installation");
    fs::rename(temporary, target);
    cleanup.path.clear();
    return inspect_plugin(target / source.manifest_path.filename());
}
void remove_plugin(const fs::path &manifest) {
    const auto info = inspect_plugin(manifest);
    auto &r = registry();
    std::lock_guard lock(r.mutex);
    if (r.enabled.contains(info.name))
        throw std::runtime_error("Disable the plugin before removing its files");
    if (auto it = r.live.find(info.library_path); it != r.live.end() && !it->second.expired())
        throw std::runtime_error("A live flame or render job still retains this plugin; close its "
                                 "snapshots before removal");
    // Delete only the two explicitly identified package files, never a whole arbitrary folder.
    fs::remove(info.library_path);
    fs::remove(info.manifest_path);
    if (fs::is_empty(info.manifest_path.parent_path()))
        fs::remove(info.manifest_path.parent_path());
}

std::string_view term_name(const Term &t) {
    if (t.id == Variation::plugin) {
        if (!t.plugin)
            throw std::invalid_argument("Plugin variation is unavailable");
        return t.plugin->info().name;
    }
    return variation_name(t.id);
}
Term make_variation_term(std::string_view name, double weight,
                         const std::map<std::string, double> &params) {
    if (!std::isfinite(weight))
        throw std::invalid_argument("Variation weight must be finite");
    if (const auto id = find_variation(name)) {
        if (!params.empty())
            throw std::invalid_argument(
                "Builtin per-term parameters are not supported; edit transform JuliaN parameters");
        return {*id, weight};
    }
    auto plugin = find_plugin_variation(name);
    if (!plugin)
        throw std::invalid_argument("Unavailable plugin or unknown variation '" +
                                    std::string(name) + "'; enable its matching plugin");
    Term t{Variation::plugin, weight, plugin, {}};
    const auto &metadata = plugin->info().parameters;
    for (const auto &p : metadata) {
        auto it = params.find(p.name);
        t.plugin_parameters.push_back(it == params.end() ? p.default_value : it->second);
    }
    for (const auto &[key, value] : params)
        if (std::none_of(metadata.begin(), metadata.end(),
                         [&](const auto &p) { return p.name == key; }))
            throw std::invalid_argument("Unknown plugin parameter '" + key + "'");
    validate_plugin_term(t);
    return t;
}
std::map<std::string, double> term_parameters(const Term &t) {
    std::map<std::string, double> result;
    if (t.id != Variation::plugin)
        return result;
    validate_plugin_term(t);
    const auto &meta = t.plugin->info().parameters;
    for (std::size_t i = 0; i < meta.size(); ++i)
        result.emplace(meta[i].name, t.plugin_parameters[i]);
    return result;
}
void validate_plugin_term(const Term &t) {
    if (t.id != Variation::plugin) {
        if (t.plugin || !t.plugin_parameters.empty())
            throw std::invalid_argument("Builtin term carries unexpected plugin state");
        return;
    }
    if (!t.plugin)
        throw std::invalid_argument("Plugin variation is unavailable");
    const auto &info = t.plugin->info();
    if (t.plugin_parameters.size() != info.parameters.size())
        throw std::invalid_argument("Wrong parameter count for plugin " + info.name);
    for (std::size_t i = 0; i < info.parameters.size(); ++i) {
        const auto &p = info.parameters[i];
        const auto v = t.plugin_parameters[i];
        if (!std::isfinite(v) || v < p.minimum || v > p.maximum ||
            (p.integer && std::trunc(v) != v))
            throw std::invalid_argument("Out-of-range plugin parameter " + info.name + "_" +
                                        p.name);
    }
}
Point PluginVariation::evaluate(Point input, const Affine &pre, double weight,
                                std::span<const double> parameters, Rng &rng) const {
    ApoPluginInputV1 in{sizeof(ApoPluginInputV1),
                        input.x,
                        input.y,
                        {pre.a, pre.b, pre.c, pre.d, pre.e, pre.f},
                        weight,
                        &rng,
                        random_value};
    ApoPluginOutputV1 out{};
    out.struct_size = sizeof(out);
    int status = -1;
    try {
        status = api_->evaluate(&in, parameters.data(),
                                static_cast<std::uint32_t>(parameters.size()), &out);
    } catch (...) {
        throw std::runtime_error("Plugin '" + info_.name +
                                 "' threw an exception during evaluation");
    }
    out.error[sizeof(out.error) - 1] = '\0';
    if (status || out.struct_size != sizeof(out) || !std::isfinite(out.x) || !std::isfinite(out.y))
        throw std::runtime_error(
            "Plugin '" + info_.name + "' failed: " +
            (out.error[0] ? std::string(out.error) : "invalid status or non-finite output"));
    return {out.x, out.y};
}
std::string plugin_identity(const Flame &flame) {
    std::map<std::string, std::string> ids;
    const auto add = [&](const Transform &t) {
        for (const auto &term : t.variations)
            if (term.id == Variation::plugin) {
                validate_plugin_term(term);
                const auto &p = term.plugin->info();
                const auto identity = p.id + "\n" + p.version + "\n" + p.fingerprint;
                const auto [it, inserted] = ids.emplace(p.name, identity);
                if (!inserted && it->second != identity)
                    throw std::invalid_argument(
                        "Flame mixes different plugin modules for variation " + p.name);
            }
    };
    for (const auto &t : flame.transforms)
        add(t);
    if (flame.final_transform)
        add(*flame.final_transform);
    std::string result;
    for (const auto &[name, value] : ids)
        result += name + "\n" + value + "\n";
    return result;
}
bool has_plugin_variations(const Flame &flame) {
    const auto has = [](const Transform &t) {
        return std::any_of(t.variations.begin(), t.variations.end(),
                           [](const auto &v) { return v.id == Variation::plugin; });
    };
    return std::any_of(flame.transforms.begin(), flame.transforms.end(), has) ||
           (flame.final_transform && has(*flame.final_transform));
}
} // namespace apo
