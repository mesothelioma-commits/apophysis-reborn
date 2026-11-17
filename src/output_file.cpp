#include "output_file.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace apo {
namespace fs = std::filesystem;
void write_atomic(const fs::path& output, const std::function<void(std::ostream&)>& write,
                  const std::function<void()>& before_commit) {
    // Reserve a private directory beside the destination, even for concurrent writers.
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temp;
    for (unsigned i = 0;; ++i) {
        temp = output.parent_path() / (".apophysis-" + std::to_string(tick) + "-" + std::to_string(i));
        if (fs::create_directory(temp)) break;
        if (i == 99) throw std::runtime_error("Unable to reserve temporary output");
    }
    struct Cleanup {
        const fs::path& dir;
        ~Cleanup() { std::error_code ec; fs::remove_all(dir, ec); }
    } cleanup{temp};
    const auto staged = temp / "content";
    std::ofstream out(staged, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    write(out);
    out.close();
    if (before_commit) before_commit();
#ifdef _WIN32
    if (!MoveFileExW(staged.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("Cannot replace output", output,
                                   std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
    fs::rename(staged, output);
#endif
}
}
