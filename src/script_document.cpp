#include "flame_io.hpp"
#include "legacy_document.hpp"
#include <algorithm>
#include <set>

namespace apo {
void LegacyDocument::apply_model(
    std::size_t index, const Flame &model,
    std::span<const std::optional<std::size_t>> transform_sources) {
  const auto original = render_model(index);
  if (original.source_version != model.source_version)
    throw std::invalid_argument(
        "Scripting cannot change a flame's producer dialect");
  validate(model);
  // Canonical attributes are used only as values. The source itself is patched,
  // retaining its encoding, comments, quotes, metadata and other library
  // entries.
  const auto mode = entries_.at(index).attributes.contains("reborn_render_values")
                        ? FlameXmlMode::render_snapshot : FlameXmlMode::canonical;
  const auto before = parse(serialize_flame(original, mode));
  const auto after = parse(serialize_flame(model, mode));
  auto working = *this;
  std::vector<Patch> patches;
  auto difference = [](const auto &old, const auto &next) {
    AttributeChanges changes;
    for (const auto &[key, value] : old) {
      const auto found = next.find(key);
      if (found == next.end())
        changes[key] = std::nullopt;
      else if (found->second != value)
        changes[key] = found->second;
    }
    for (const auto &[key, value] : next)
      if (!old.contains(key))
        changes[key] = value;
    return changes;
  };
  auto header =
      difference(before.entries_[0].attributes, after.entries_[0].attributes);
  if (header.contains("rotate"))
    header["angle"] = std::nullopt;
  if (header.contains("oversample"))
    header["supersample"] = std::nullopt;
  if (header.contains("filter") && !model.filter_radius)
    header["filter_shape"] = std::nullopt;
  attribute_patches(ranges_.at(index), entries_[index].attributes,
                    std::move(header), patches);
  const auto &sources = sources_.at(index);
  const auto &old_sources = before.sources_[0];
  const auto &new_sources = after.sources_[0];
  const std::set<std::string> geometry{"weight",   "color",   "color_speed",
                                       "symmetry", "opacity", "coefs",
                                       "post",     "chaos"};
  const auto transform_changes = [&](const TransformSource &old,
                                     const TransformSource &next) {
    auto changes = difference(old.attributes, next.attributes);
    if (changes.contains("color_speed") || changes.contains("symmetry")) {
      changes["color_speed"] = std::nullopt;
      changes["symmetry"] = std::nullopt;
      for (const auto *key : {"color_speed", "symmetry"})
        if (const auto it = next.attributes.find(key);
            it != next.attributes.end())
          changes[key] = it->second;
    }
    if (changes.contains("opacity"))
      changes["plotmode"] = std::nullopt;
    const bool variation_change =
        std::any_of(changes.begin(), changes.end(), [&](const auto &item) {
          return !geometry.contains(item.first) && !find_variation_parameter(item.first);
        });
    if (variation_change) {
      for (const auto *key : {"var", "var1", "linear3D", "var_order", "flatten",
                              "sinusoidal_use3D"})
        changes[key] = std::nullopt;
      for (const auto &[key, value] : old.attributes)
        if (!geometry.contains(key))
          changes[key] = std::nullopt;
      for (const auto &[key, value] : next.attributes)
        if (!geometry.contains(key))
          changes[key] = value;
    }
    return changes;
  };
  if (!transform_sources.empty() &&
      transform_sources.size() != model.transforms.size())
    throw std::invalid_argument("Transform source mapping has the wrong size");
  const auto origin = [&](std::size_t target) -> std::optional<std::size_t> {
    if (transform_sources.empty())
      return target < sources.transforms.size()
                 ? std::optional<std::size_t>(target)
                 : std::nullopt;
    const auto source = transform_sources[target];
    if (source && *source >= sources.transforms.size())
      throw std::invalid_argument(
          "Transform source identity is outside the document");
    return source;
  };
  // New canonical elements may contain Unicode plugin identity values. Convert
  // only non-ASCII characters to XML references before encoding the fragment.
  const auto fragment = [&](const TransformSource &source) {
    const auto text = after.bytes_.substr(
        source.range.begin, source.range.end - source.range.begin);
    std::string ascii;
    for (std::size_t i = 0; i < text.size();) {
      const auto lead = static_cast<unsigned char>(text[i++]);
      if (lead < 128) {
        ascii += static_cast<char>(lead);
        continue;
      }
      const unsigned continuation = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : 1;
      unsigned code = lead & (0x7fU >> (continuation + 1));
      for (unsigned j = 0; j < continuation; ++j)
        code = code * 64 + (static_cast<unsigned char>(text.at(i++)) & 63);
      ascii += "&#" + std::to_string(code) + ';';
    }
    return encode_ascii(ascii);
  };
  const auto source_changes = [&](std::size_t target,
                                  std::size_t source_index) {
    auto changes = transform_changes(old_sources.transforms[source_index],
                                     new_sources.transforms[target]);
    if (!is_apophysis(model.source_version) &&
        !sources.transforms[source_index].attributes.contains("color") &&
        (target != source_index ||
         bool(original.final_transform) != bool(model.final_transform)))
      changes["color"] = new_sources.transforms[target].attributes.at("color");
    return changes;
  };
  for (std::size_t i = 0; i < sources.transforms.size(); ++i) {
    const auto &slot = sources.transforms[i];
    if (i >= new_sources.transforms.size()) {
      patches.push_back({slot.range.begin, slot.range.end, {}});
      continue;
    }
    const auto from = origin(i);
    if (from && *from == i)
      attribute_patches(slot.range, slot.attributes, source_changes(i, *from),
                        patches);
    else {
      auto text = from ? edited_element(sources.transforms[*from],
                                        source_changes(i, *from))
                       : fragment(new_sources.transforms[i]);
      patches.push_back({slot.range.begin, slot.range.end, std::move(text)});
    }
  }
  const std::string newline = encode_ascii(
      bytes_.find(encode_ascii("\r\n")) != std::string::npos ? "\r\n  "
                                                             : "\n  ");
  const auto insertion = sources.transforms.back().range.end;
  std::string extra;
  for (std::size_t i = sources.transforms.size();
       i < new_sources.transforms.size(); ++i) {
    const auto from = origin(i);
    extra += newline + (from ? edited_element(sources.transforms[*from],
                                              source_changes(i, *from))
                             : fragment(new_sources.transforms[i]));
  }
  if (model.final_transform) {
    const auto &next = *new_sources.final_transform;
    if (original.final_transform && sources.final_transform)
      attribute_patches(
          sources.final_transform->range, sources.final_transform->attributes,
          transform_changes(*old_sources.final_transform, next), patches);
    else {
      const auto final_fragment = fragment(next);
      if (sources.final_transform)
        patches.push_back({sources.final_transform->range.begin,
                           sources.final_transform->range.end, final_fragment});
      else
        extra += newline + final_fragment;
    }
  } else if (original.final_transform && sources.final_transform)
    patches.push_back({sources.final_transform->range.begin,
                       sources.final_transform->range.end,
                       {}});
  if (original.final_transform && model.final_transform &&
      sources.final_transform && !is_apophysis(model.source_version) &&
      original.transforms.size() != model.transforms.size() &&
      original.final_transform->color == model.final_transform->color &&
      !sources.final_transform->attributes.contains("color")) {
    attribute_patches(
        sources.final_transform->range, sources.final_transform->attributes,
        {{"color", new_sources.final_transform->attributes.at("color")}},
        patches);
  }
  if (!extra.empty())
    patches.push_back({insertion, insertion, std::move(extra)});
  working.commit(std::move(patches), index);
  working.edit_palette(index, model.palette);
  // The producer's parser may quantize legacy colors; this matches normal
  // edits.
  (void)working.render_model(index);
  *this = std::move(working);
}
} // namespace apo
