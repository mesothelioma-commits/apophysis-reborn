#include "animation_export.hpp"
#include "flame_io.hpp"
#include "sha256.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <string_view>

namespace {
using namespace apo;
namespace fs = std::filesystem;
void require(bool value, std::string_view message) {
  if (!value)
    throw std::runtime_error(std::string(message));
}
template <class Exception = std::exception, class F>
std::string rejects(F &&function, std::string_view message) {
  try {
    function();
  } catch (const Exception &error) {
    return error.what();
  }
  throw std::runtime_error(std::string(message));
}
std::string read(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  require(bool(in), "Cannot read test output");
  return {std::istreambuf_iterator<char>(in), {}};
}
void write(const fs::path &path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), std::streamsize(bytes.size()));
  require(bool(out), "Cannot write test fixture");
}
AnimationExportOptions options(const fs::path &root, std::string_view name) {
  AnimationExportOptions result;
  result.output_directory = root / std::string(name);
  fs::create_directory(result.output_directory);
  result.first = 0;
  result.last = 2;
  result.render.samples = 16384;
  result.render.threads = 1;
  result.render.memory_mib = 32;
  result.file.disk_mib = 64;
  return result;
}
fs::path state(const AnimationExportOptions &options) {
  return options.state_directory.empty()
             ? options.output_directory / (".apo-sequence-" + options.prefix)
             : options.state_directory;
}
fs::path frame(const AnimationExportOptions &options, unsigned index) {
  return options.output_directory / (options.prefix + "_00000" + std::to_string(index) + ".png");
}
void same_frames(const AnimationExportOptions &a, const AnimationExportOptions &b) {
  for (unsigned i = 0; i < 3; ++i)
    require(read(frame(a, i)) == read(frame(b, i)), "Resumed image differs from uninterrupted image");
}
void hashes(const fs::path &root) {
  require(detail::sha256_bytes("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "Empty SHA-256 vector changed");
  require(detail::sha256_bytes("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc vector changed");
  const std::string million(1'000'000, 'a');
  const auto expected = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
  require(detail::sha256_bytes(million) == expected, "Streaming SHA-256 block vector changed");
  write(root / "hash.bin", million);
  require(detail::sha256_file(root / "hash.bin") == expected, "File SHA-256 differs from byte SHA-256");
  rejects([&] { detail::sha256_file(root / "hash.bin", {}, 100); }, "Hash input limit was ignored");
  std::stop_source stop;
  stop.request_stop();
  rejects<RenderCancelled>([&] { detail::sha256_file(root / "hash.bin", stop.get_token()); },
                           "File hashing ignored cancellation");
}
void test(const fs::path &root) {
  hashes(root);
  auto model = demo_flame();
  model.width = 24;
  model.height = 16;
  model.scale = 8;
  const auto source = LegacyDocument::parse(serialize_flame(model));
  auto clip = make_animation_preset(source, 0, AnimationPreset::zoom, 3, {30000, 1001}, UINT64_MAX);
  const PreparedAnimation animation(clip);
  auto clean = options(root, "clean");
  const auto planned = plan_animation_export(animation, clean);
  require(planned.frames.size() == 3 && planned.width == 24 && planned.height == 16,
          "Preflight did not select the expected fixed canvas/range");
  require(planned.frames[0].destination.filename() == "frame_000000.png" &&
              planned.frames[2].destination.filename() == "frame_000002.png",
          "Frame numbers are not zero-based global padded indices");
  for (const auto &f : planned.frames)
    require(f.seed == UINT64_MAX, "Fixed full-width seed was not preserved");
  require(!fs::exists(state(clean)) && !fs::exists(frame(clean, 0)),
          "Read-only sequence preflight created state or outputs");
  const auto completed = export_animation_sequence(animation, clean);
  require(completed.completed == 3 && completed.rendered == 3 && !completed.verified_existing,
          "Initial sequence result is incorrect");
  require(fs::is_regular_file(completed.manifest) && fs::is_empty(state(clean) / "checkpoints"),
          "Completed frame checkpoints were not released");
  auto resumed = clean;
  resumed.resume = true;
  const auto verified = export_animation_sequence(animation, resumed);
  require(verified.completed == 3 && !verified.rendered && verified.verified_existing == 3,
          "Resume did not verify and skip exactly completed files");
  const auto journal = read(completed.manifest);
  auto changed = resumed;
  ++changed.render.samples;
  require(rejects([&] { plan_animation_export(animation, changed); }, "Changed sampling resumed")
              .find("identity mismatch") != std::string::npos,
          "Changed sequence settings did not diagnose an identity mismatch");
  auto other_clip = clip;
  other_clip.name = "Changed clip";
  rejects([&] { plan_animation_export(PreparedAnimation(other_clip), resumed); },
          "Changed clip metadata resumed silently");
  auto original = read(frame(clean, 1));
  auto damaged = original;
  damaged[damaged.size() / 2] ^= 1;
  write(frame(clean, 1), damaged);
  require(rejects([&] { export_animation_sequence(animation, resumed); },
                  "Corrupt completed image was skipped").find("missing or changed") != std::string::npos,
          "Completed image corruption was not diagnosed");
  require(read(completed.manifest) == journal && read(frame(clean, 1)) == damaged,
          "Corruption preflight modified manifest or user output");
  fs::remove(frame(clean, 1));
  rejects([&] { export_animation_sequence(animation, resumed); }, "Missing completed image was skipped");
  write(frame(clean, 1), original);
  auto broken_journal = journal;
  broken_journal[broken_journal.size() / 2] ^= 1;
  write(completed.manifest, broken_journal);
  require(rejects([&] { plan_animation_export(animation, resumed); }, "Damaged journal resumed")
              .find("checksum") != std::string::npos,
          "Manifest integrity failure was not diagnosed");
  write(completed.manifest, journal);

  auto partial = options(root, "partial");
  std::stop_source stop;
  partial.render.stop_token = stop.get_token();
  partial.progress = [&](const AnimationExportProgress &p) {
    if (p.phase == "completed" && p.completed == 1)
      stop.request_stop();
  };
  rejects<RenderCancelled>([&] { export_animation_sequence(animation, partial); },
                           "Sequence ignored cancellation between frames");
  require(fs::exists(frame(partial, 0)) && !fs::exists(frame(partial, 1)),
          "Cancellation did not retain exactly completed frames");
  partial.render.stop_token = {};
  partial.progress = {};
  partial.render.threads = 2; // Resource scheduling is not rendering identity.
  partial.resume = true;
  const auto resumed_partial = export_animation_sequence(animation, partial);
  require(resumed_partial.rendered == 2 && resumed_partial.verified_existing == 1,
          "Partial sequence did not resume remaining frames");
  same_frames(clean, partial);

  auto sample_partial = options(root, "sample-partial");
  std::stop_source sample_stop;
  sample_partial.render.stop_token = sample_stop.get_token();
  sample_partial.render.progress = [&](std::uint64_t done, std::uint64_t) {
    if (done >= 8192)
      sample_stop.request_stop();
  };
  rejects<RenderCancelled>([&] { export_animation_sequence(animation, sample_partial); },
                           "Sequence ignored per-frame sampling cancellation");
  require(!fs::exists(frame(sample_partial, 0)) &&
              has_render_checkpoint(state(sample_partial) / "checkpoints" / "frame_000000"),
          "Sample cancellation lost its committed frame checkpoint");
  sample_partial.render.stop_token = {};
  sample_partial.render.progress = {};
  sample_partial.resume = true;
  export_animation_sequence(animation, sample_partial);
  same_frames(clean, sample_partial);

  // Exercise both sides of the durable ready/publication crash window.
  for (bool already_published : {false, true}) {
    auto ready = options(root, already_published ? "ready-published" : "ready-private");
    ready.last = 0;
    std::stop_source ready_stop;
    ready.render.stop_token = ready_stop.get_token();
    ready.progress = [&](const AnimationExportProgress &p) {
      if (p.phase == "ready")
        ready_stop.request_stop();
    };
    rejects<RenderCancelled>([&] { export_animation_sequence(animation, ready); },
                             "Ready-stage pause was ignored");
    require(!fs::exists(frame(ready, 0)), "Ready-stage cancellation published prematurely");
    const auto staged = state(ready) / "rendered" / "frame_000000.png";
    require(fs::is_regular_file(staged), "Ready-stage image was not retained");
    if (already_published)
      fs::copy_file(staged, frame(ready, 0));
    ready.render.stop_token = {};
    ready.progress = {};
    ready.resume = true;
    const auto result = export_animation_sequence(animation, ready);
    require(result.completed == 1 && result.rendered == 0 &&
                read(frame(ready, 0)) == read(frame(clean, 0)),
            "Ready-stage resume rerendered or changed the completed image");
  }

  auto overwrite = options(root, "overwrite");
  for (unsigned i = 0; i < 3; ++i)
    write(frame(overwrite, i), "prior output " + std::to_string(i));
  rejects([&] { plan_animation_export(animation, overwrite); }, "Existing frame was overwritten implicitly");
  require(!fs::exists(state(overwrite)), "Overwrite rejection created sequence state");
  overwrite.replace_existing = true;
  std::stop_source overwrite_stop;
  overwrite.render.stop_token = overwrite_stop.get_token();
  overwrite.progress = [&](const AnimationExportProgress &p) {
    if (p.phase == "completed")
      overwrite_stop.request_stop();
  };
  rejects<RenderCancelled>([&] { export_animation_sequence(animation, overwrite); }, "Overwrite pause failed");
  require(read(frame(overwrite, 1)) == "prior output 1", "Cancellation replaced an unrendered prior image");
  overwrite.resume = true;
  overwrite.replace_existing = false; // The immutable prior stamps preserve initial authorization.
  overwrite.render.stop_token = {};
  overwrite.progress = {};
  write(frame(overwrite, 1), "new user work");
  rejects([&] { export_animation_sequence(animation, overwrite); }, "Changed pending destination was overwritten");
  require(read(frame(overwrite, 1)) == "new user work", "Resume destroyed changed user work");
  write(frame(overwrite, 1), "prior output 1");
  export_animation_sequence(animation, overwrite);
  same_frames(clean, overwrite);

  auto racing = options(root, "destination-race");
  racing.last = 0;
  racing.progress = [&](const AnimationExportProgress &p) {
    if (p.phase == "ready")
      write(frame(racing, 0), "user output created during render");
  };
  rejects([&] { export_animation_sequence(animation, racing); }, "Concurrent output was overwritten");
  require(read(frame(racing, 0)) == "user output created during render", "Publication destroyed concurrent user output");

  auto locked = options(root, "locked");
  locked.last = 0;
  bool attempted_concurrent_resume = false;
  locked.progress = [&](const AnimationExportProgress &p) {
    if (p.phase == "rendering" && !attempted_concurrent_resume) {
      attempted_concurrent_resume = true;
      auto concurrent = locked;
      concurrent.progress = {};
      concurrent.resume = true;
      require(rejects([&] { export_animation_sequence(animation, concurrent); },
                      "Concurrent sequence writer acquired the state lock").find("in use") != std::string::npos,
              "Concurrent resume did not diagnose the held process lock");
    }
  };
  require(export_animation_sequence(animation, locked).completed == 1 && attempted_concurrent_resume,
          "Process-lock rejection disrupted the original sequence writer");

  auto names = options(root, "global-names");
  names.first = 1;
  names.last = 2;
  names.step = 2;
  const auto range = plan_animation_export(animation, names);
  require(range.frames.size() == 1 && range.frames[0].frame == 1 &&
              range.frames[0].destination.filename() == "frame_000001.png",
          "Stepped inclusive range silently renumbered or included an out-of-step endpoint");
  auto seeds = clip;
  seeds.seed_mode = AnimationSeedMode::per_frame;
  const auto varied = plan_animation_export(PreparedAnimation(seeds), options(root, "seeds"));
  require(varied.frames[0].seed == animation_seed(seeds, 0) &&
              varied.frames[1].seed == animation_seed(seeds, 1) &&
              varied.frames[0].seed != varied.frames[1].seed,
          "Per-frame seed mode did not use deterministic timeline seeds");
  auto canvas = options(root, "canvas");
  canvas.width = 48;
  canvas.height = 16;
  const auto fitted = animation_export_flame(animation.evaluate(0), 48, 16);
  require(fitted.scale == animation.evaluate(0).flame.scale && fitted.width == 48 && fitted.height == 16,
          "Fixed canvas fit changed the wrong camera scale");
  require(plan_animation_export(animation, canvas).width == 48, "Explicit fixed canvas was ignored");
  canvas.height = 0;
  rejects([&] { plan_animation_export(animation, canvas); }, "Incomplete canvas override was accepted");

  auto invalid = options(root, "invalid");
  for (const auto &prefix : {"../outside", "frame/name", "frame:name", "trailing ", "", "a.b"}) {
    invalid.prefix = prefix;
    rejects([&] { plan_animation_export(animation, invalid); }, "Unsafe portable prefix was accepted");
  }
  invalid.prefix = "frame";
  invalid.step = 0;
  rejects([&] { plan_animation_export(animation, invalid); }, "Zero frame step was accepted");
  invalid.step = 1;
  invalid.last = 3;
  rejects([&] { plan_animation_export(animation, invalid); }, "Out-of-range final frame was accepted");
  invalid.last = 2;
  invalid.protected_files = {frame(invalid, 0)};
  rejects([&] { plan_animation_export(animation, invalid); }, "Input/output collision was accepted");
  invalid.protected_files = {state(invalid) / "original.flame"};
  rejects([&] { plan_animation_export(animation, invalid); }, "State/input overlap was accepted");
  invalid.protected_files.clear();
  invalid.file.checkpoint_directory = root / "caller-owned";
  rejects([&] { plan_animation_export(animation, invalid); }, "Caller supplied checkpoint directory was accepted");
  invalid.file.checkpoint_directory.clear();
  invalid.file.disk_mib = 1;
  rejects([&] { plan_animation_export(animation, invalid); }, "Complete sequence disk budget was ignored");
  invalid.file.disk_mib = 64;
  invalid.last = 1000;
  auto long_clip = make_animation_preset(source, 0, AnimationPreset::still, 1001);
  rejects([&] { plan_animation_export(PreparedAnimation(long_clip), invalid); }, "1001-frame batch was silently accepted/truncated");
  invalid.last = 2;
  invalid.resume = true;
  rejects([&] { plan_animation_export(animation, invalid); }, "Missing sequence state was inferred from files");
  invalid.resume = false;
  std::stop_source before;
  before.request_stop();
  invalid.render.stop_token = before.get_token();
  rejects<RenderCancelled>([&] { export_animation_sequence(animation, invalid); }, "Pre-cancelled export continued");
  require(!fs::exists(state(invalid)), "Failed/cancelled preflight created state");

  auto links = options(root, "links");
  const auto target = root / "outside";
  fs::create_directory(target);
  std::error_code link_error;
  fs::create_directory_symlink(target, state(links), link_error);
  if (!link_error) {
    rejects([&] { plan_animation_export(animation, links); }, "Symlink state directory was followed");
    fs::remove(state(links));
  }
  write(frame(links, 0), "existing image");
  fs::create_hard_link(frame(links, 0), frame(links, 1), link_error);
  if (!link_error) {
    links.replace_existing = true;
    rejects([&] { plan_animation_export(animation, links); }, "Hard-linked output collision was accepted");
  }
}
} // namespace
int main() {
  const auto root = fs::temp_directory_path() /
      ("apo-animation-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directory(root);
  try {
    test(root);
    fs::remove_all(root);
    std::cout << "Animation sequence export tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
