#include "document_cli.hpp"
#include "flame_io.hpp"
#include "legacy_document.hpp"
#include "output_file.hpp"

#include <charconv>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace apo {
namespace {
namespace fs = std::filesystem;

std::string utf8(const fs::path& arg) {
    const auto bytes = arg.u8string();
    return {bytes.begin(), bytes.end()};
}

template<class T>
T number(const fs::path& path, const std::string& field) {
    const auto text = utf8(path);
    T value{};
    const char* end = text.data() + text.size();
    const auto read = std::from_chars(text.data(), end, value);
    if (read.ec != std::errc{} || read.ptr != end || !std::isfinite(static_cast<double>(value)))
        throw std::invalid_argument("Invalid number for " + field);
    return value;
}

std::string json_string(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string text = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') text.push_back('\\');
        if (c < 32) {
            text += "\\u00";
            text.push_back(hex[c >> 4]);
            text.push_back(hex[c & 15]);
        } else text.push_back(static_cast<char>(c));
    }
    return text + '"';
}

void inspect(const LegacyDocument& document, bool json) {
    if (json) std::cout << "{\"entries\":[";
    for (std::size_t i = 0; i < document.entries().size(); ++i) {
        const auto& entry = document.entries()[i];
        const auto issue = document.render_issue(i);
        const auto status = issue ? "preserve-only" : "renderable";
        if (json) {
            if (i) std::cout << ',';
            std::cout << "{\"index\":" << i + 1 << ",\"name\":" << json_string(entry.name)
                      << ",\"version\":" << json_string(entry.version) << ",\"line\":" << entry.line
                      << ",\"status\":" << json_string(status) << ",\"first_issue\":"
                      << (issue ? json_string(*issue) : "null") << "}";
        } else {
            std::cout << i + 1 << "  " << json_string(entry.name) << "  " << status;
            if (!entry.version.empty()) std::cout << "  " << json_string(entry.version);
            std::cout << "  (line " << entry.line << ")\n";
            if (issue) std::cout << "    First render blocker: " << json_string(*issue) << '\n';
        }
    }
    if (json) std::cout << "]}\n";
}
}

int document_command(const std::vector<fs::path>& args) {
    const auto command = utf8(args.front());
    if (args.size() < 2) throw std::invalid_argument(command + " needs an input flame file");
    const auto& input = args[1];
    if (command == "inspect") {
        const bool json = args.size() == 3 && args[2] == "--json";
        if (args.size() != 2 && !json) throw std::invalid_argument("inspect accepts INPUT and optional --json");
        inspect(LegacyDocument::load(input), json);
        return 0;
    }
    LegacyEdits edits;
    fs::path output;
    std::size_t index = 1;
    std::set<std::string> seen;
    for (std::size_t i = 2; i < args.size();) {
        const auto option = utf8(args[i++]);
        if (!seen.insert(option).second) throw std::invalid_argument("Duplicate option " + option);
        auto next = [&]() -> const fs::path& {
            if (i == args.size()) throw std::invalid_argument("Missing value for " + option);
            return args[i++];
        };
        if (option == "--index") index = number<std::size_t>(next(), option);
        else if (option == "--output") output = next();
        else if (command == "save" && option == "--name") edits.name = utf8(next());
        else if (command == "save" && option == "--scale") edits.scale = number<double>(next(), option);
        else if (command == "save" && option == "--size")
            edits.size = std::array{number<int>(next(), option), number<int>(next(), option)};
        else if (command == "save" && option == "--center")
            edits.center = Point{number<double>(next(), option), number<double>(next(), option)};
        else throw std::invalid_argument("Unknown option for " + command + ": " + option);
    }
    if (output.empty()) throw std::invalid_argument(command + " requires --output PATH");
    auto document = LegacyDocument::load(input);
    if (!index || index > document.entries().size()) throw std::invalid_argument("Flame index out of range");
    if (command == "save") {
        document.edit(index - 1, edits);
        write_atomic(output, [&](auto& out){ out << document.bytes(); });
        std::cout << "Saved collection with " << document.entries().size() << " flame(s).\n";
    } else {
        if (fs::exists(output) && fs::equivalent(input, output))
            throw std::invalid_argument("Use a distinct output for single-flame model export");
        const auto xml = serialize_flame(document.render_model(index - 1));
        write_atomic(output, [&](auto& out){ out << xml; });
        std::cout << "Exported selected render model as standalone XML.\n";
    }
    return 0;
}
}
