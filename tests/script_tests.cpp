#include "creative.hpp"
#include "flame_io.hpp"
#include "script.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

namespace {
using namespace apo;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void near(double a, double b, const char *message, double eps = 3e-12) {
  require(std::isfinite(a) && std::abs(a - b) < eps, message);
}
LegacyDocument fixture() {
  return LegacyDocument::parse(serialize_flame(demo_flame()));
}
Flame model(const ScriptResult &r) {
  return LegacyDocument::parse(r.document_bytes).render_model(r.selected_index);
}
void rejects(const std::function<void()> &fn,
             const std::string &expected = {}) {
  try {
    fn();
  } catch (const ScriptError &e) {
    if (!expected.empty() &&
        std::string(e.what()).find(expected) == std::string::npos)
      throw std::runtime_error("Wrong rejection: " + std::string(e.what()) +
                               "; expected " + expected);
    return;
  }
  throw std::runtime_error("Expected script rejection: " + expected);
}
ScriptResult legacy(const LegacyDocument &d, std::string_view source,
                    ScriptOptions opts = {}) {
  opts.language = ScriptLanguage::legacy_pascal;
  return run_script(d, 0, source, opts);
}
void modern_edit_and_preserve() {
  const auto base = fixture();
  std::string bytes = base.bytes();
  const auto first = bytes.find("<flame ");
  bytes.insert(first, "<!-- keep library annotation -->\n");
  const auto tag = bytes.find("<flame ") + 7;
  bytes.insert(tag, "nick='Original artist' url='https://example.invalid/' ");
  const auto transform = bytes.find("<xform ") + 7;
  bytes.insert(transform, "name='hand-edited transform' ");
  const auto d = LegacyDocument::parse(bytes);
  const auto untouched =
      run_script(d, 0, "assert(apo.count()==1); print('unchanged')");
  require(untouched.document_bytes == bytes, "No-op script rewrote source");
  const auto r = run_script(d, 0, R"LUA(
        local settings=apo.flame(); settings.gamma=2.7; settings.name='Script & study'
        apo.set_flame(settings)
        local t=apo.transform(1);t.pre[5]=0.375;t.color=.25
        t.variations={{name='julian',weight=.8},{name='curl',weight=.2,parameters={c1=.375,c2=.1}}}
        t.julian_power=3;t.julian_dist=.6;apo.set_transform(1,t)
        assert(apo.transform(1).variations[2].parameters.c1==.375)
        apo.palette_from_stops({{0,0,0,0},{255,1,.5,.25}})
        print('done',apo.transform_count())
    )LUA");
  const auto f = model(r);
  near(f.gamma, 2.7, "gamma edit");
  near(f.transforms[0].pre.e, .375, "affine edit");
  require(f.name == "Script & study", "escaped name edit");
  require(f.transforms[0].parameters.julian_power == 3, "parameter edit");
  near(f.transforms[0].parameters.curl_c1, .375, "script builtin parameter");
  near(f.palette[128].g, 128. / 510, "palette interpolation");
  require(r.document_bytes.find("<!-- keep library annotation -->") !=
              std::string::npos,
          "comment lost");
  require(r.document_bytes.find("nick='Original artist'") != std::string::npos,
          "metadata lost");
  require(r.document_bytes.find("name='hand-edited transform'") !=
              std::string::npos,
          "transform metadata lost");
  require(d.bytes() == bytes, "input document changed");
}
void construction_and_rollback() {
  const auto d = fixture();
  const auto r = run_script(d, 0, R"LUA(
        apo.clear_transforms()
        apo.add_transform{weight=0,variations={linear=0}}
        apo.add_transform{pre={.5,0,0,.5,1,-1},weight=0,variations={linear=0}}
        apo.set_transform(1,{weight=.5,variations={linear=1}})
        apo.set_transform(2,{weight=.5,variations={swirl=.2,linear=.8}})
        apo.set_final_transform{pre={1,0,0,1,0,.1},weight=1,color_speed=0}
    )LUA");
  require(model(r).transforms.size() == 2 &&
              model(r).final_transform.has_value(),
          "temporary construction state failed");
  rejects(
      [&] {
        run_script(d, 0,
                   "apo.set_flame{name='must roll back'}; error('deliberate')");
      },
      "deliberate");
  rejects([&] { run_script(d, 0, "apo.clear_transforms()"); }, "transform");
  rejects(
      [&] {
        run_script(d, 0,
                   "apo.set_transform(1,{variations={not_a_variation=1}})");
      },
      "variation");
  rejects([&] { run_script(d, 0, "apo.set_flame{typo=1}"); }, "Unknown field");
  rejects(
      [&] {
        run_script(d, 0,
                   "apo.set_transform(1,{variations={{name='linear',parameters="
                   "{unknown=1}}}})");
      },
      "parameter");
  require(d.bytes() == fixture().bytes(), "failure leaked edits");
}
void source_aliases_and_utf16() {
  std::string xml =
      "<flame name='Aliases' version='Apophysis 2.09' angle='0.2' "
      "supersample='1' nick='KEEP' size='100 80' scale='20'>\n<!-- keep "
      "-->\n<xform weight='1' color_speed='0.2' plotmode='on' var1='0' "
      "coefs='1 0 0 1 0 0'/><color index='0' rgb='0 0 0'/><color index='255' "
      "rgb='255 255 255'/></flame>";
  std::string middle;
  for (int i = 1; i < 255; ++i)
    middle += "<color index='" + std::to_string(i) + "' rgb='" +
              std::to_string(i) + " " + std::to_string(i) + " " +
              std::to_string(i) + "'/>";
  xml.insert(xml.find("</flame>"), middle);
  const auto d = LegacyDocument::parse(xml);
  const auto r =
      run_script(d, 0,
                 "apo.set_flame{rotate=30,oversample=2};apo.set_transform(1,{"
                 "color_speed=.75,opacity=.25,variations={swirl=1}})");
  const auto f = model(r);
  near(f.rotate, 30, "angle alias conversion");
  require(f.oversample == 2, "supersample alias conversion");
  near(f.transforms[0].color_speed, .75, "symmetry alias");
  near(f.transforms[0].opacity, .25, "plot mode alias");
  require(r.document_bytes.find("nick='KEEP'") != std::string::npos &&
              r.document_bytes.find("<!-- keep -->") != std::string::npos,
          "alias merge lost metadata");
  std::string utf16 = "\xff\xfe";
  for (char ch : xml) {
    utf16 += ch;
    utf16 += '\0';
  }
  const auto wide = LegacyDocument::parse(utf16);
  const auto changed =
      run_script(wide, 0,
                 "apo.set_flame{name='Café & "
                 "Ω',gamma=2.5};apo.set_transform(1,{color=.7})");
  require(changed.document_bytes.starts_with("\xff\xfe"), "UTF-16 BOM lost");
  require(model(changed).name == "Café & Ω", "UTF-16 Unicode edit");
  near(model(changed).gamma, 2.5, "UTF-16 gamma edit");
}
void topology_final_color() {
  const auto d = LegacyDocument::parse(
      "<flame name='Topology'><xform weight='1' linear='1'/><finalxform "
      "linear='1'/><color index='0' rgb='0 0 0'/><color index='255' rgb='255 "
      "255 255'/></flame>");
  const double color = d.render_model(0).final_transform->color;
  const auto r =
      run_script(d, 0, "apo.add_transform{weight=1,variations={linear=1}}");
  near(model(r).final_transform->color, color,
       "implicit final color changed after append");
}
void topology_source_identity() {
  auto f = demo_flame();
  f.transforms.resize(2);
  f.transforms[0].weight = f.transforms[1].weight = 1;
  std::string xml = serialize_flame(f);
  std::size_t p = xml.find("<xform ") + 7;
  xml.insert(p, "name='A' ");
  p = xml.find("<xform ", p + 1) + 7;
  xml.insert(p, "name='B' ");
  const auto d = LegacyDocument::parse(xml);
  const auto r = run_script(d, 0,
                            "apo.remove_transform(1);apo.set_transform(1,{"
                            "weight=0});apo.set_transform(1,{weight=1})");
  require(r.document_bytes.find("name='B'") != std::string::npos &&
              r.document_bytes.find("name='A'") == std::string::npos,
          "removed transform stole survivor metadata");
  const auto copied = legacy(d, "begin ActiveTransform:=1;CopyTransform;end.");
  auto first = copied.document_bytes.find("name='B'");
  require(first != std::string::npos &&
              copied.document_bytes.find("name='B'", first + 1) !=
                  std::string::npos,
          "copied source metadata lost");
  const auto cleared = run_script(
      d, 0, "apo.clear_transforms();apo.add_transform{};apo.add_transform{}");
  require(cleared.document_bytes.find("name='A'") == std::string::npos &&
              cleared.document_bytes.find("name='B'") == std::string::npos,
          "new transforms inherited cleared metadata");
  const auto final = LegacyDocument::parse(
      "<flame><xform weight='1' linear='1'/><finalxform linear='1'/><color "
      "index='0' rgb='0 0 0'/></flame>");
  const auto edit = run_script(
      final, 0, "apo.add_transform{};apo.set_final_transform{color=.3}");
  near(model(edit).final_transform->color, .3, "final color patched twice");
}
void producer_snapshot_consistency() {
  auto f = demo_flame();
  f.source_version = "Apophysis 7x 15C";
  f.filter_radius = .5;
  const auto d = LegacyDocument::parse(serialize_flame(f));
  ScriptOptions options;
  options.allow_render_requests = true;
  const auto r =
      run_script(d, 0,
                 "apo.add_transform{variations={linear=1,pre_blur=.1}};apo."
                 "render('producer.png',{width=32,height=24,samples=1000})",
                 options);
  const auto after = model(r);
  const auto &captured = r.requests[0].flame.transforms.back();
  require(captured.parameters.apophysis_math ==
                  after.transforms.back().parameters.apophysis_math &&
              captured.parameters.separate_blur_angle ==
                  after.transforms.back().parameters.separate_blur_angle,
          "producer flags in request differ from document");
  require(captured.parameters.apophysis_math &&
              captured.parameters.separate_blur_angle,
          "new 7X transform producer flags missing");
  near(r.requests[0].flame.scale, after.scale * 32 / after.width,
       "request resize changed field of view");
}
void deterministic_generation() {
  const auto d = fixture();
  const std::string source = R"LUA(
        local first=apo.generate{seed='18446744073709551615',count=2,min_transforms=3,max_transforms=3,variations={'linear','swirl'}}
        apo.select(first);apo.mutate{seed=123,strength=.2,colors=true,parameters=true}
        apo.randomseed('18446744073709551615');print(apo.random(),math.random(1,50));apo.random_palette(17,4)
    )LUA";
  const auto a = run_script(d, 0, source), b = run_script(d, 0, source);
  require(a.document_bytes == b.document_bytes && a.console == b.console,
          "seeded scripts changed between runs");
  require(LegacyDocument::parse(a.document_bytes).entries().size() == 3,
          "generation did not append batch");
  const auto name = model(a).name;
  require(!name.empty(), "generated flame unnamed");
  ScriptOptions options;
  options.seed = 99;
  const auto r = run_script(d, 0, "print(apo.random())", options);
  Rng rng(99);
  require(!r.console.empty(), "random console missing");
  const double random = std::stod(r.console[0]);
  near(random, rng.unit(), "modern random stream differs from core", 1e-13);
}
void requests_and_capabilities() {
  const auto d = fixture();
  ScriptOptions options;
  options.allow_export_requests = true;
  options.allow_render_requests = true;
  const auto r = run_script(d, 0, R"LUA(
        apo.set_flame{name='captured'};apo.export_flame('study.flame');apo.export_palette('palette.gpl')
        apo.render('frames/one.png',{samples=1000,seed='18446744073709551615',width=64,height=48,format='png16',alpha=true})
        apo.set_flame{name='after capture'};apo.export_library('library.flame')
    )LUA",
                            options);
  require(r.requests.size() == 4, "request count");
  require(LegacyDocument::parse(r.requests[0].bytes).render_model(0).name ==
              "captured",
          "export snapshot changed later");
  require(r.requests[2].flame.name == "captured" &&
              r.requests[2].flame.width == 64,
          "render snapshot changed later");
  require(r.requests[2].render.seed == UINT64_MAX, "uint64 seed was rounded");
  require(model(r).name == "after capture", "successful document not returned");
  require(r.requests[2].file.checkpoint_directory.empty() &&
              !r.requests[2].file.resume,
          "script selected checkpoint path");
  rejects([&] { run_script(d, 0, "apo.export_flame('x.flame')"); }, "disabled");
  rejects([&] { run_script(d, 0, "apo.render('x.png')"); }, "disabled");
  for (const auto *path :
       {"../escape.flame", "/absolute.flame", "C:\\escape.flame",
        "one/../../escape.flame", "one//two.flame", "one./two.flame"}) {
    const auto escaped = [&] {
      std::string s;
      for (char ch : std::string(path)) {
        if (ch == '\\')
          s += '\\';
        s += ch;
      }
      return s;
    }();
    rejects(
        [&] {
          run_script(d, 0, "apo.export_flame('" + escaped + "')", options);
        },
        "path");
  }
  rejects(
      [&] {
        run_script(d, 0, "apo.export_flame('x');apo.export_flame('x')",
                   options);
      },
      "Duplicate");
  options.limits.requests = 1;
  rejects(
      [&] {
        run_script(d, 0, "apo.export_flame('x');apo.export_flame('y')",
                   options);
      },
      "request count");
  options.limits.requests = 64;
  options.limits.host_memory_bytes = 600000;
  rejects(
      [&] {
        run_script(
            d, 0, "for i=1,50 do apo.export_library(tostring(i)..'.flame') end",
            options);
      },
      "host memory");
  ScriptOptions imports;
  imports.imports.emplace("approved", d.bytes());
  require(run_script(
              d, 0,
              "apo.import_library('approved');apo.set_flame{name='Imported'}",
              imports)
                  .selected_index == 0,
          "named import failed");
  rejects(
      [&] { run_script(d, 0, "apo.import_library('/etc/passwd')", imports); },
      "not supplied");
}
void sandbox_and_limits() {
  const auto d = fixture();
  const auto r = run_script(d, 0, R"LUA(
        assert(_VERSION=='Lua 5.5');assert(io==nil and os==nil and package==nil and require==nil and debug==nil)
        assert(load==nil and loadfile==nil and dofile==nil and coroutine==nil)
        assert(pcall==nil and xpcall==nil and setmetatable==nil and getmetatable==nil)
        assert(string.dump==nil and string.match==nil and string.gsub==nil and string.find==nil)
        assert(utf8.len('Ω')==1 and math.sqrt(9)==3)
        local t={3,1,2};table.sort(t);assert(table.concat(t,',')=='1,2,3')
    )LUA");
  require(r.document_bytes == d.bytes(), "sandbox checks mutated document");
  ScriptOptions options;
  options.limits.instructions = 4000;
  rejects([&] { run_script(d, 0, "while true do end", options); },
          "instruction limit");
  options = ScriptOptions{};
  options.limits.lua_memory_bytes = 200000;
  rejects([&] { run_script(d, 0, "local x=string.rep('a',1000000)", options); },
          "memory");
  options.limits.lua_memory_bytes = 100;
  rejects([&] { run_script(d, 0, "return", options); }, "memory");
  options = ScriptOptions{};
  options.limits.host_memory_bytes = 1;
  rejects([&] { run_script(d, 0, "return", options); }, "host memory");
  options = ScriptOptions{};
  options.limits.lua_memory_bytes = 200000;
  rejects(
      [&] {
        run_script(d, 0, "local x=string.rep('a',100000);error(123)", options);
      },
      "non-string");
  rejects([&] { run_script(d, 0, "error({})"); }, "non-string");
  try {
    run_script(d, 0, "error(string.rep('x',20000))");
    throw std::runtime_error("long error accepted");
  } catch (const ScriptError &e) {
    require(std::string(e.what()).size() < 4200, "unbounded Lua diagnostic");
  }
  options = ScriptOptions{};
  options.limits.console_bytes = 8;
  rejects([&] { run_script(d, 0, "print('12345678')", options); }, "console");
  options = ScriptOptions{};
  options.limits.console_bytes = 32;
  rejects(
      [&] {
        run_script(d, 0, "local s=string.rep('x',32);print(s,s,s,s,s)",
                   options);
      },
      "console");
  options = ScriptOptions{};
  options.limits.host_calls = 4;
  rejects([&] { run_script(d, 0, "for i=1,10 do apo.flame() end", options); },
          "host call");
  options = ScriptOptions{};
  options.limits.source_bytes = 3;
  rejects([&] { run_script(d, 0, "print(1)", options); }, "source");
  options = ScriptOptions{};
  options.limits.timeout_ms = 1;
  options.limits.instructions = UINT64_MAX;
  rejects([&] { run_script(d, 0, "while true do end", options); },
          "time limit");
  options = ScriptOptions{};
  options.limits.transforms_per_flame = d.render_model(0).transforms.size();
  rejects([&] { run_script(d, 0, "apo.add_transform{}", options); },
          "transform count");
  try {
    run_script(d, 0, "local x = 1\nlocal y =\nreturn x");
    throw std::runtime_error("syntax error accepted");
  } catch (const ScriptError &e) {
    require(e.line() > 0, "Lua syntax line missing");
  }
}
void cancellation() {
  const auto d = fixture();
  std::stop_source stop;
  stop.request_stop();
  ScriptOptions options;
  options.stop_token = stop.get_token();
  bool stopped = false;
  try {
    run_script(d, 0, "apo.set_flame{name='bad'}", options);
  } catch (const ScriptCancelled &) {
    stopped = true;
  }
  require(stopped, "pre-cancel did not stop");
  stop = std::stop_source{};
  options.stop_token = stop.get_token();
  options.limits.instructions = UINT64_MAX;
  std::jthread trigger([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    stop.request_stop();
  });
  stopped = false;
  try {
    run_script(d, 0, "apo.set_flame{name='bad'};while true do end", options);
  } catch (const ScriptCancelled &) {
    stopped = true;
  }
  require(stopped, "running script did not stop");
  require(d.bytes() == fixture().bytes(), "cancel leaked changes");
}
void legacy_language_and_model() {
  const auto d = fixture();
  const auto r = legacy(d, R"ASC(
{ comments are parsed, not translated as executable text }
var i, j: Integer; total: Double;
begin
 Clear; Flame.Name := 'Pascal''s triangle'; Flame.Width := 720; Flame.Height := 480;
 Flame.Scale := 50; Flame.Angle := Pi/2; Flame.Time := 2.5;
 total := 0;
 for i := 0 to 2 do begin
   AddTransform; Transform.a := 0.5; Transform.d := 0.5;
   Transform.e := Cos(i * 2 * Pi/3); Transform.f := Sin(i * 2 * Pi/3);
   Transform.Color := i / 2; Transform.Weight := 1;
   Transform.Variation[VariationIndex('linear')] := 1;
   total := total + i;
 end;
 j := 0; while j < 2 do Inc(j);
 repeat Dec(j); until j = 0;
 if (total = 3) and not (j <> 0) then Flame.Gamma := 2.4 else Flame.Gamma := 1;
 for i := 0 to 255 do begin
   Flame.Gradient[i,0] := i; Flame.Gradient[i,1] := 255-i; Flame.Gradient[i,2] := 0;
 end;
 Flame.Background[0] := 17;
 Print('Created ' + IntToStr(Transforms));
end.
)ASC");
  const auto f = model(r);
  require(f.name == "Pascal's triangle" && f.transforms.size() == 3,
          "legacy construction");
  near(f.scale, 360, "legacy Flame.Scale percent-of-width");
  near(f.rotate, -90, "legacy Flame.Angle radians");
  near(f.time, 2.5, "legacy time metadata");
  near(f.gamma, 2.4, "legacy loops/condition");
  near(f.transforms[1].pre.e, -.5, "legacy trig");
  near(f.palette[128].r, 128. / 255, "legacy gradient");
  near(f.background.r, 17. / 255, "legacy background");
  require(r.console.size() == 1 && r.console[0] == "Created 3",
          "legacy print/concatenation");
  require(!r.translated_source.empty() && !r.compatibility_notes.empty(),
          "legacy translation report missing");
}
void legacy_affine_oracle() {
  const auto d = fixture();
  const auto r = legacy(d, R"ASC(begin
      Clear;AddTransform;
      Transform.a:=0.4;Transform.c:=0.2;Transform.b:=-0.3;Transform.d:=0.5;Transform.e:=1;Transform.f:=-2;
      Rotate(37);Scale(0.75);Translate(1.2,-0.7);Multiply(0.9,-0.4,0.2,1.1);
    end.)ASC");
  const auto t = model(r).transforms[0];
  // Outputs of the actual Apophysis 2.09 TXForm.Mul33/Identity/Rotate/Scale/
  // Translate/Multiply methods, extracted unchanged and compiled with
  // FPC 3.2.2. Original source archive SHA-256 and exact experiment are in
  // LEGACY_SCRIPTING.md.
  near(t.pre.a, .28472677975059152, "original Pascal Rotate/Scale/Multiply a");
  near(t.pre.b, -.19876926107717069, "original Pascal Rotate/Scale/Multiply b");
  near(t.pre.c, .12836821882462862, "original Pascal Rotate/Scale/Multiply c");
  near(t.pre.d, .45999130855608933, "original Pascal Rotate/Scale/Multiply d");
  near(t.pre.e, 1.43835991409135850, "original Pascal Translate e");
  near(t.pre.f, -2.24924251482820910, "original Pascal Translate f");
  const auto coordinates = legacy(d, R"ASC(begin Clear;AddTransform;
      Transform.b:=2;Transform.c:=3;
      if (Transform.Coefs[1,0]<>-2) or (Transform.Coefs[0,1]<>-3) then Flame.Name:='wrong' else Flame.Name:='right';
      Transform.Coefs[2,1]:=4;Transform.Post[0,1]:=5;Transform.Post[1,0]:=6;
      Transform.Symmetry:=0.6;
    end.)ASC");
  const auto c = model(coordinates);
  require(c.name == "right", "legacy B/C or coefficient signs wrong");
  near(c.transforms[0].pre.f, -4, "coefs Y sign");
  near(c.transforms[0].post.b, -5, "post b sign");
  near(c.transforms[0].post.c, -6, "post c sign");
  near(c.transforms[0].color_speed, .2, "legacy symmetry conversion");
}
void legacy_variations_and_requests() {
  const auto d = fixture();
  ScriptOptions options;
  options.allow_export_requests = true;
  options.allow_render_requests = true;
  const auto r = legacy(d, R"ASC(begin
      ActiveTransform:=0;Transform.Clear;
      Transform.Linear:=0;Transform.JuliaN:=1;Transform.julian_power:=3;Transform.julian_dist:=0.5;
      Transform.Color:=0.25;Transform.Weight:=0.5;
      Renderer.Width:=32;Renderer.Height:=24;Renderer.Filename:='legacy.png';
      Flame.Quality:=1;Render;SaveFlame('legacy.flame');
      Print(Round(2.5));Print(-7 div 3);Print(-7 mod 3);
    end.)ASC",
                        options);
  require(r.requests.size() == 2 && r.requests[0].flame.width == 32,
          "legacy deferred render");
  require(model(r).transforms[0].parameters.julian_power == 3,
          "legacy named parameter");
  require(r.console == std::vector<std::string>({"2", "-2", "-1"}),
          "legacy Round/div/mod semantics");
  const auto normalized =
      legacy(d, "begin "
                "ActiveTransform:=0;Transform.Clear;Transform.Linear:=-2;"
                "Transform.Swirl:=2;NormalizeVars;end.");
  const auto normalized_model = model(normalized);
  const auto &terms = normalized_model.transforms[0].variations;
  for (const auto &t : terms)
    if (t.weight != 0)
      near(t.weight, .5, "original NormalizeVars takes absolute weights");
}
void legacy_strict_preflight() {
  const auto d = fixture();
  for (const auto *bad :
       {"begin Flame.Name:='changed'; DeleteFile('x');end.",
        "begin Clear; UnknownOperation;end.", "begin Options.Gamma:=2;end.",
        "begin Transform.Variable[0]:=2;end.",
        "begin with Transform do a:=1;end.",
        "procedure Hack; begin end; begin Hack;end.",
        "begin Flame.Name := #65;end.", "begin Transforms:=3;end.",
        "begin Print(UnknownProperty);end.", "begin Print(1);end.."}) {
    rejects([&] { legacy(d, bad); }, "Legacy script");
    require(d.bytes() == fixture().bytes(), "preflight rejection leaked edits");
  }
  rejects(
      [&] {
        legacy(d, "var i:Integer;begin Flame.Name:='private';i:=1.5;end.");
      },
      "integer");
  rejects([&] { legacy(d, "var s:String;begin s:=3;end."); }, "string");
  rejects([&] { legacy(d, "begin for i:=1 to 3 do Inc(i);end."); },
          "active for-loop variable");
  ScriptOptions small_output;
  small_output.limits.console_bytes = 8;
  rejects([&] { legacy(d, "begin Print('12345678');end.", small_output); },
          "console");
  ScriptOptions options;
  options.limits.instructions = 2000;
  rejects([&] { legacy(d, "begin while true do begin end;end.", options); },
          "instruction limit");
  try {
    legacy(d, "begin\nPrint('x');\nOptions.Gamma:=2;\nend.");
    throw std::runtime_error("legacy error accepted");
  } catch (const ScriptError &e) {
    require(e.line() == 3, "legacy diagnostic line wrong");
  }
}
void bundled_examples() {
  const auto d = fixture();
  ScriptOptions options;
  options.allow_export_requests = true;
  options.allow_render_requests = true;
  for (const auto &e : script_examples()) {
    options.language = e.language;
    const auto r = run_script(d, 0, e.source, options);
    validate(model(r));
  }
}
} // namespace
int main() {
  const std::pair<const char *, void (*)()> tests[]{
      {"modern editing/source preservation", modern_edit_and_preserve},
      {"construction/rollback", construction_and_rollback},
      {"aliases/UTF-16", source_aliases_and_utf16},
      {"topology/final color", topology_final_color},
      {"topology/source identity", topology_source_identity},
      {"producer snapshot consistency", producer_snapshot_consistency},
      {"deterministic generation", deterministic_generation},
      {"requests/capabilities", requests_and_capabilities},
      {"sandbox/limits", sandbox_and_limits},
      {"cancellation", cancellation},
      {"legacy language/model", legacy_language_and_model},
      {"legacy original Pascal oracle", legacy_affine_oracle},
      {"legacy variations/requests", legacy_variations_and_requests},
      {"legacy strict preflight", legacy_strict_preflight},
      {"bundled examples", bundled_examples}};
  for (const auto &[name, test] : tests) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &e) {
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
      return 1;
    }
  }
  return 0;
}
