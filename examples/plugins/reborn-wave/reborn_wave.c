/* Original Reborn example: a small affine-aware sine displacement.
 * Build as a shared library with src/plugin_api.h on the include path.
 */
#include "plugin_api.h"
#include <math.h>
static const ApoPluginParameterV1 parameters[] = {
    {"amplitude", 0.2, -2.0, 2.0, APO_PLUGIN_PARAMETER_REAL},
    {"frequency", 3.0, 0.0, 20.0, APO_PLUGIN_PARAMETER_REAL}};
static int APO_PLUGIN_CALL evaluate(const ApoPluginInputV1 *in, const double *p, uint32_t count,
                                    ApoPluginOutputV1 *out) {
    if (!in || !out || count != 2 || in->struct_size != sizeof(*in) ||
        out->struct_size != sizeof(*out))
        return 1;
    out->x = in->x + p[0] * sin(p[1] * in->y);
    out->y = in->y + p[0] * sin(p[1] * in->x);
    return 0;
}
static const ApoPluginV1 plugin = {sizeof(ApoPluginV1),
                                   APO_PLUGIN_ABI_V1,
                                   "org.apophysis.reborn.wave",
                                   "sample_wave",
                                   "1.0.0",
                                   "Ordinary 2D sine displacement",
                                   2,
                                   parameters,
                                   evaluate};
APO_PLUGIN_EXPORT const ApoPluginV1 *APO_PLUGIN_CALL apo_plugin_query(uint32_t abi) {
    return abi == APO_PLUGIN_ABI_V1 ? &plugin : 0;
}
