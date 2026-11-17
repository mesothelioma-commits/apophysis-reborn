#include "flame.hpp"
#include "flame_io.hpp"
#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace apo;

bool close(double actual, double expected) {
    return std::isfinite(actual) && std::abs(actual - expected) <=
        1e-12 * std::max(1.0, std::abs(expected));
}

void test_vectors(const char* tsv_path, const char* geometry_path) {
    const auto fixtures = load_flames(geometry_path);
    if (fixtures.size() != 2 || fixtures[0].transforms.size() != 1 || fixtures[1].transforms.size() != 1)
        throw std::runtime_error("unexpected reference geometry collection");
    std::ifstream file(tsv_path);
    std::string line;
    const std::string header = "kind\tx\ty\told_color\tbranch\texpected_x\texpected_y\texpected_color";
    if (!std::getline(file, line) || line != header)
        throw std::runtime_error("missing or invalid reference table");
    unsigned count = 0;
    while (std::getline(file, line)) {
        std::istringstream row(line);
        Orbit input, expected;
        int kind = -1, branch = -1;
        if (!(row >> kind >> input.point.x >> input.point.y >> input.color >> branch
                  >> expected.point.x >> expected.point.y >> expected.color) ||
            kind < 0 || kind > 16 || (branch != 0 && branch != 1))
            throw std::runtime_error("invalid vector at row " + std::to_string(count + 2));
        Transform t;
        if (kind <= 14) {
            t.variations = {{static_cast<Variation>(kind), 1}};
            t.color = .75;
            t.color_speed = .25;
        } else {
            t = fixtures.at(kind - 15).transforms.front();
        }
        // With this engine's mt19937_64, seeds 0 and 2 choose opposite Julia branches.
        Rng rng(branch ? 0 : 2);
        const auto actual = apply_transform(t, input, rng);
        if (!close(actual.point.x, expected.point.x) || !close(actual.point.y, expected.point.y) ||
            !close(actual.color, expected.color)) {
            std::ostringstream error;
            error << std::setprecision(17) << "FLAM3 mismatch on row " << count + 2
                  << " (kind " << kind << ", branch " << branch << ") at "
                  << input.point.x << ", " << input.point.y << ": expected "
                  << expected.point.x << ", " << expected.point.y << ", " << expected.color
                  << " got " << actual.point.x << ", " << actual.point.y << ", " << actual.color;
            throw std::runtime_error(error.str());
        }
        ++count;
    }
    if (!file.eof() || count != 126)
        throw std::runtime_error("reference table must contain exactly 126 vectors");
    std::cout << "126 upstream FLAM3 transform vectors matched\n";
}

void test_weights() {
    Flame f;
    f.transforms.resize(4);
    f.transforms[0].weight = f.transforms[3].weight = 0;
    f.transforms[1].weight = 1;
    f.transforms[2].weight = 3;
    f.transforms[1].chaos = {0, 3, 1};
    f.transforms[2].chaos = {0, 1, 0, 0};
    validate(f);
    const TransformChooser chooser(f);
    const double almost_one = std::nextafter(1., 0.);
    if (chooser.pick(0) != 1 || chooser.pick(std::nextafter(.25, 0.)) != 1 ||
        chooser.pick(.25) != 2 || chooser.pick(almost_one) != 2)
        throw std::runtime_error("base selection boundaries or zero-weight exclusion");
    if (chooser.pick(std::nextafter(.5, 0.), 1) != 1 || chooser.pick(.5, 1) != 2 ||
        chooser.pick(almost_one, 2) != 1)
        throw std::runtime_error("relative destination weights or forbidden transitions");

    // A common factor cancels, even if multiplying the raw factors would overflow or underflow.
    for (double factor : {std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()}) {
        Flame extreme;
        extreme.transforms.resize(2);
        for (auto& transform : extreme.transforms) transform.weight = factor;
        extreme.transforms.front().chaos = {factor, factor};
        validate(extreme);
        TransformChooser scaled(extreme);
        if (scaled.pick(.25, 0) != 0 || scaled.pick(.75, 0) != 1)
            throw std::runtime_error("selection changed under extreme uniform weight scaling");
    }
}

void test_reference_image(const char* flame_path, const char* reference_path) {
    const auto flames = load_flames(flame_path);
    if (flames.size() != 1 || flames.front().width != 192 || flames.front().height != 144 ||
        default_samples(flames.front()) != 3317760)
        throw std::runtime_error("unexpected FLAM3 image comparison settings");
    RenderOptions options;
    options.samples = default_samples(flames.front());
    options.threads = 1;
    options.seed = 27;
    const auto result = render(flames.front(), options);
    const auto& pixels = result.image.rgb;

    std::ifstream ppm(reference_path, std::ios::binary);
    std::string line;
    for (const auto expected : {"P6", "192 144", "255"}) {
        if (!std::getline(ppm, line) || line != expected)
            throw std::runtime_error("unexpected FLAM3 reference image header");
    }
    std::vector<unsigned char> reference(pixels.size());
    if (!ppm.read(reinterpret_cast<char*>(reference.data()), reference.size()) ||
        ppm.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("invalid FLAM3 reference image size");

    double abs_error = 0, squared_error = 0;
    double sum_x = 0, sum_y = 0, sum_xx = 0, sum_yy = 0, sum_xy = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 3) {
        double x = 0, y = 0;
        for (std::size_t c = 0; c < 3; ++c) {
            const double delta = int(pixels[i + c]) - int(reference[i + c]);
            abs_error += std::abs(delta);
            squared_error += delta * delta;
            x += pixels[i + c];
            y += reference[i + c];
        }
        // Pearson correlation is unchanged by using a sum instead of an RGB mean.
        sum_x += x;
        sum_y += y;
        sum_xx += x*x;
        sum_yy += y*y;
        sum_xy += x*y;
    }
    const double n = pixels.size() / 3;
    const double corr = (n*sum_xy - sum_x*sum_y) /
        std::sqrt((n*sum_xx - sum_x*sum_x) * (n*sum_yy - sum_y*sum_y));
    const double mae = abs_error / pixels.size();
    std::cout << std::fixed << std::setprecision(6) << "FLAM3 image comparison: RGB MAE "
              << mae << ", RGB RMSE " << std::sqrt(squared_error / pixels.size())
              << ", intensity correlation " << corr << '\n';
    // These thresholds describe this fixture at its declared quality and seed.
    if (!(mae <= 6.5 && corr >= .975))
        throw std::runtime_error("render diverged from the pinned FLAM3 reference image");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("expected a vector table, geometry fixture, image flame and reference PPM");
        test_vectors(argv[1], argv[2]);
        test_weights();
        test_reference_image(argv[3], argv[4]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
