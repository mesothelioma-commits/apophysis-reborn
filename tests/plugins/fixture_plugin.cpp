#include "plugin_api.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
struct LoadMarker {
    LoadMarker() {
        if (const auto path = std::getenv("APO_PLUGIN_TEST_MARKER"))
            if (auto file = std::fopen(path, "ab")) {
                std::fputs("loaded\n", file);
                std::fclose(file);
            }
    }
} marker;
const ApoPluginParameterV1 parameters[] = {{"mode", 0, 0, 4, APO_PLUGIN_PARAMETER_INTEGER}};
int APO_PLUGIN_CALL evaluate(const ApoPluginInputV1 *in, const double *p, uint32_t count,
                             ApoPluginOutputV1 *out) {
    if (!in || !out || count != 1 || in->struct_size != sizeof(*in) ||
        out->struct_size != sizeof(*out))
        return 7;
    out->x = in->x;
    out->y = in->y;
    switch (static_cast<int>(p[0])) {
    case 1:
        out->x = std::numeric_limits<double>::quiet_NaN();
        break;
    case 2:
        std::memcpy(out->error, "fixture failure", 16);
        return 9;
    case 3:
        out->x += in->random_unit(in->random_context);
        out->y += in->random_unit(in->random_context);
        break;
    case 4:
        out->x = in->affine[0] + in->affine[2] + in->affine[4];
        out->y = in->affine[1] + in->affine[3] + in->affine[5] + in->weight;
        break;
    }
    return 0;
}
const ApoPluginV1 descriptor = {sizeof(ApoPluginV1),
#ifdef APO_TEST_BAD_ABI
                                999,
                                "org.apophysis.reborn.fixture.badabi",
                                "fixture_badabi",
#else
                                APO_PLUGIN_ABI_V1,
                                "org.apophysis.reborn.fixture",
                                "fixture_probe",
#endif
                                "1.0.0",
                                "Deterministic plugin contract fixture",
                                1,
                                parameters,
                                evaluate};
} // namespace
APO_PLUGIN_EXPORT const ApoPluginV1 *APO_PLUGIN_CALL apo_plugin_query(uint32_t abi) {
    return abi == APO_PLUGIN_ABI_V1 ? &descriptor : nullptr;
}
