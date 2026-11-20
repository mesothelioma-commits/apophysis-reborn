#include "flame.hpp"
#include "flame_io.hpp"
#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

void check_m11_producers();

namespace {
using namespace apo;
constexpr std::array<Variation, 10> additions{
    Variation::waves, Variation::fisheye, Variation::popcorn, Variation::exponential,
    Variation::power, Variation::cosine, Variation::rings, Variation::fan,
    Variation::eyefish, Variation::cylinder};
constexpr std::array<Affine, 2> matrices{{
    {.69, .21, -.37, .52, .33, -.46}, {-.41, .63, .28, -.71, -.57, .26}}};

void check(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double actual, double expected) {
    return std::isfinite(actual) && std::abs(actual - expected) <=
        1e-12 * std::max(1., std::abs(expected));
}

void independent_vectors(const char *filename) {
    std::ifstream stream(filename);
    std::string line;
    check(bool(std::getline(stream, line)) && line ==
          "producer\tkind\tmatrix\tweight\tx\ty\texpected_x\texpected_y", "Invalid oracle header");
    unsigned count = 0;
    std::array<unsigned, 2> producers{};
    while (std::getline(stream, line)) {
        std::istringstream row(line);
        std::string producer, extra;
        unsigned kind, matrix;
        double weight;
        Point input, expected;
        check(bool(row >> producer >> kind >> matrix >> weight >> input.x >> input.y >>
                   expected.x >> expected.y) && !(row >> extra), "Malformed oracle row");
        check(matrix < matrices.size() && (producer == "flam3" || producer == "apo209"),
              "Invalid oracle producer or matrix");
        Transform transform;
        transform.pre = matrices[matrix];
        transform.post = {.91, -.13, .27, .75, -.05, .08};
        transform.variations = {{static_cast<Variation>(kind), weight}};
        transform.parameters.apophysis_math = producer == "apo209";
        Rng rng(123);
        const auto actual = apply_transform(transform, {input, .4}, rng).point;
        if (!close(actual.x, expected.x) || !close(actual.y, expected.y)) {
            std::ostringstream error;
            error << std::setprecision(17) << producer << " " << variation_name(static_cast<Variation>(kind))
                  << " row " << count + 2 << ": expected " << expected.x << ", " << expected.y
                  << "; obtained " << actual.x << ", " << actual.y;
            throw std::runtime_error(error.str());
        }
        ++count;
        ++producers[producer == "apo209"];
    }
    check(stream.eof() && count == 640 && producers[0] == 320 && producers[1] == 320,
          "Expected 320 FLAM3 and 320 original Apophysis transform vectors");
    std::cout << "640 independently generated FLAM3/Apophysis transform vectors matched\n";
}

std::string document(const std::string &version, const std::string &term) {
    return "<flame version='" + version + "'><xform weight='1' " + term +
           " coefs='.69 .21 -.37 .52 .33 -.46'/><palette count='256' format='RGB'>" +
           std::string(1536, 'f') + "</palette></flame>";
}

void import_and_replay() {
    check(variation_registry().size() == 45, "Expected 45 implemented registry entries");
    for (auto id : additions) {
        const std::string name(variation_name(id));
        check(find_variation(name) == id, "Registry lookup mismatch");
        const unsigned number = static_cast<unsigned>(id);
        const unsigned classic = id == Variation::eyefish ? 23 : id == Variation::cylinder ? 25 : number;
        for (const auto &version : {std::string{}, std::string("Apophysis 2.09")}) {
            const auto named = parse_flames(document(version, name + "='-.375'")).front();
            const auto numeric = parse_flames(document(version, "var1='" +
                std::to_string(version.empty() ? number : classic) + "'")).front();
            check(named.transforms[0].variations[0].id == id &&
                  named.transforms[0].variations[0].weight == -.375 &&
                  numeric.transforms[0].variations[0].id == id, "Named/numeric import: " + name);
            const auto reopened = parse_flames(serialize_flame(named)).front();
            Rng original(17), copy(17), untouched(17);
            const auto p = apply_transform(named.transforms[0], {{-.31, .73}, .5}, original);
            const auto q = apply_transform(reopened.transforms[0], {{-.31, .73}, .5}, copy);
            check(close(p.point.x, q.point.x) && close(p.point.y, q.point.y),
                  "Saved affine-coupled variation changed: " + name);
            check(original.unit() == untouched.unit(), "Deterministic variation consumed random state");
        }
    }
    // A mixed numeric array must use the classic registry, including the gap to FLAM3's IDs.
    std::string weights;
    for (unsigned i = 0; i <= 25; ++i) weights += (i == 15 || i == 23 || i == 25) ? " .25" : " 0";
    const auto mixture = parse_flames(document("Apophysis 2.09", "var='" + weights + "'")).front();
    const auto &terms = mixture.transforms[0].variations;
    check(terms.size() == 3 && terms[0].id == Variation::waves && terms[1].id == Variation::eyefish &&
          terms[2].id == Variation::cylinder, "Classic numeric array mapping");
    Rng rng(9);
    VariationParameters classic;
    classic.apophysis_math = true;
    for (auto id : {Variation::power, Variation::rings}) {
        const auto point = apply_variation(id, {}, rng, classic);
        check(point.x == 0 && point.y == 0, "Classic zero-radius guard");
    }
    for (auto id : {Variation::waves, Variation::fisheye, Variation::popcorn, Variation::fan,
                    Variation::eyefish, Variation::cylinder}) {
        const auto point = apply_variation(id, {}, rng);
        check(std::isfinite(point.x) && std::isfinite(point.y), "Zero affine/coordinate singularity");
    }
    const auto swapped = apply_variation(Variation::fisheye, {0, 1}, rng);
    const auto unswapped = apply_variation(Variation::eyefish, {0, 1}, rng);
    check(swapped.x == 1 && swapped.y == 0 && unswapped.x == 0 && unswapped.y == 1,
          "Historical fisheye and eyefish axes must differ");
}

// These fixtures come from separately compiled FLAM3 operations and precomputations.
void starting_vectors(const char *filename) {
    std::ifstream input(filename);
    std::string row;
    check(std::getline(input, row) && row ==
          "name\tattributes\tx\ty\tweight\tseed\texpected_x\texpected_y\tnext_draw",
          "Invalid starting-variation oracle header");
    std::map<std::string, unsigned> counts;
    while (std::getline(input, row)) {
        std::istringstream fields(row);
        std::string name, parameters, trailing;
        std::getline(fields, name, '\t');
        std::getline(fields, parameters, '\t');
        Point point, expected;
        double weight, nextDraw;
        unsigned seed;
        check((fields >> point.x >> point.y >> weight >> seed >> expected.x >> expected.y >> nextDraw) &&
              !(fields >> trailing), "Malformed oracle row for " + name);
        if (parameters == "-") parameters.clear();
        const auto xml = "<flame><xform weight='1' " + name + "='" + std::to_string(weight) +
                         "' " + parameters + "/><color index='0' rgb='0 0 0'/><color index='255' rgb='255 255 255'/></flame>";
        const auto flame = parse_flames(xml).front();
        Rng rng(seed);
        const auto actual = apply_transform(flame.transforms.front(), {point, .4}, rng).point;
        if (!close(actual.x, expected.x) || !close(actual.y, expected.y) || rng.unit() != nextDraw) {
            std::ostringstream error;
            error << std::setprecision(17) << "FLAM3 " << name << " at " << point.x << ',' << point.y
                  << " weight " << weight << " seed " << seed << ": expected " << expected.x << ','
                  << expected.y << " obtained " << actual.x << ',' << actual.y << " or random stream differs";
            throw std::runtime_error(error.str());
        }
        ++counts[name];
    }
    check(input.eof() && counts.size() == 12, "Expected all 12 added variations");
    for (const auto &[name, count] : counts) check(count == 36, "Incomplete oracle rows for " + name);
    std::cout << "432 independent starting-variation geometry and RNG vectors matched\n";
}

void studies(const char *filename) {
    const auto flames = load_flames(filename);
    check(flames.size() == variation_registry().size(), "Every registered variation needs one sample");
    for (const auto &entry : variation_registry()) {
        const auto id = entry.id;
        auto found = std::find_if(flames.begin(), flames.end(),
            [&](const Flame &flame) { return flame.name == variation_name(id); });
        check(found != flames.end(), "Missing variation study");
        auto flame = *found;
        flame.scale *= .25;
        flame.width /= 4;
        flame.height /= 4;
        RenderOptions options;
        options.threads = 1;
        options.samples = 80000;
        options.seed = 123456;
        const auto image = render(flame, options);
        const auto [low, high] = std::minmax_element(image.image.rgb.begin(), image.image.rgb.end());
        check(image.invalid == 0 && image.plotted > options.samples / 5 && *high > *low + 20,
              "Study did not produce a well-framed nonuniform image: " + flame.name);
    }
    std::cout << "All 45 variation studies rendered and passed picture-content checks\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 4, "Expected the classic and starting oracle TSVs and variation-studies collection");
        independent_vectors(argv[1]);
        import_and_replay();
        studies(argv[2]);
        starting_vectors(argv[3]);
        check_m11_producers();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Extended variation tests: " << e.what() << '\n';
        return 1;
    }
}
