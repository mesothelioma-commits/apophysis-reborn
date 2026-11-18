#include "flame_io.hpp"
#include "project_document.hpp"
#include "script_host.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>

namespace {
using namespace apo;
namespace fs = std::filesystem;
void require(bool v, const char *s) {
  if (!v)
    throw std::runtime_error(s);
}
void rejects(const std::function<void()> &f, const char *message) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, message);
}
std::string read(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
} // namespace
int main() {
  const auto root =
      fs::temp_directory_path() /
      ("apo-script-host-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    fs::create_directories(root / "outputs");
    fs::create_directories(root / "outside");
    ScriptResult result;
    ScriptRequest request;
    request.kind = ScriptRequestKind::export_flame;
    request.destination = "nested/study.flame";
    request.bytes = serialize_flame(demo_flame());
    result.requests.push_back(request);
    const auto paths = plan_script_outputs(result, root / "outputs");
    require(paths.size() == 1 && !fs::exists(paths[0].parent_path()),
            "planning must not create directories");
    fs::create_directories(paths[0].parent_path());
    publish_script_export(request, paths[0]);
    require(read(paths[0]) == request.bytes,
            "atomic export writes captured bytes");
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "existing output requires explicit replacement");
    require(plan_script_outputs(result, root / "outputs", true) == paths,
            "replacement opt-in");
    std::vector<fs::path> protectedFiles{paths[0]};
    rejects(
        [&] {
          plan_script_outputs(result, root / "outputs", true, protectedFiles);
        },
        "inputs remain protected even with replacement");
    for (const auto name :
         {"../escape.flame", "/outside.flame", "C:/outside.flame",
          "folder\\escape.flame", "NUL.txt", "dir/CON", "a/../b.flame",
          "dot./x", "space /x", "bad?.png"}) {
      result.requests[0].destination = name;
      rejects([&] { plan_script_outputs(result, root / "outputs", true); },
              "unsafe portable path admitted");
    }
    result.requests[0].destination = "duplicate.flame";
    result.requests.push_back(result.requests[0]);
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "duplicate destinations admitted");
    result.requests.resize(1);
    result.requests[0].destination = "linked/escape.flame";
    std::error_code error;
    fs::create_directory_symlink(root / "outside", root / "outputs/linked",
                                 error);
    if (!error)
      rejects([&] { plan_script_outputs(result, root / "outputs"); },
              "symlink escaped selected output root");
    result.requests[0].destination = "hardlink.flame";
    error.clear();
    fs::create_hard_link(paths[0], root / "outputs/hardlink.flame", error);
    if (!error)
      rejects(
          [&] {
            plan_script_outputs(result, root / "outputs", true, protectedFiles);
          },
          "hardlink replaced protected input");
    result.requests[0].kind = ScriptRequestKind::render;
    result.requests[0].destination = "small.png";
    result.requests[0].flame = demo_flame();
    result.requests[0].render.samples = 1000;
    result.requests[0].file.checkpoint_directory = "untrusted";
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "script controlled checkpoint path");
    result.requests[0].file.checkpoint_directory.clear();
    result.requests[0].file.resume = true;
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "script controlled resume");
    result.requests[0].file.resume = false;
    result.requests[0].destination = "wrong.jpg";
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "format mismatch survived preflight");
    result.requests[0] = request;
    result.requests[0].destination = "ancestor";
    result.requests.push_back(request);
    result.requests[1].destination = "ancestor/child.flame";
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "ancestor output collision survived preflight");
    result.requests.resize(1);
    result.requests[0].destination = "future-checkpoint/chunk";
    std::vector<fs::path> directories{root / "outputs/future-checkpoint"};
    rejects(
        [&] {
          plan_script_outputs(result, root / "outputs", true, directories);
        },
        "nonexistent protected directory child admitted");
#ifdef _WIN32
    result.requests[0].destination = fs::path(u8"café.flame");
    result.requests.push_back(request);
    result.requests[1].destination = fs::path(u8"CAFÉ.flame");
    rejects([&] { plan_script_outputs(result, root / "outputs"); },
            "Unicode ordinal duplicate admitted on Windows");
    result.requests.resize(1);
    std::vector<fs::path> unicodeProtected{root / "outputs" /
                                           fs::path(u8"CAFÉ.flame")};
    rejects(
        [&] {
          plan_script_outputs(result, root / "outputs", true, unicodeProtected);
        },
        "Unicode protected destination admitted on Windows");
#endif
    ScriptResult empty;
    require(plan_script_outputs(empty, {}).empty(),
            "no output permission needed without requests");
    const auto bytes = serialize_flame(demo_flame());
    ProjectFile project;
    project.library_bytes = bytes;
    project.metadata["retain"] = "exact";
    auto parsed = parse_document_file(serialize_project(project));
    require(parsed.project && parsed.document.bytes() == bytes,
            "native document reader");
    auto marker = bytes;
    marker.insert(marker.find('>') + 1,
                  "<!-- <apophysis-project> is just a comment -->");
    require(!parse_document_file(marker).project,
            "comment must not change document type");
    project.selected_index = 100;
    rejects([&] { parse_document_file(serialize_project(project)); },
            "invalid project selection admitted");
    fs::remove_all(root);
    std::cout << "Script host and document boundary tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
