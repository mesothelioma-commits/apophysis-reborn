/* Original teaching example for porting a weighted legacy 2D calculation.
 * See README.md. This is not a copy of a historical Apophysis plugin.
 */
#include "plugin_api.h"
static const ApoPluginParameterV1 parameters[] = {{"gain", 1, 0, 4, APO_PLUGIN_PARAMETER_REAL},
                                                  {"offset", 0, -2, 2, APO_PLUGIN_PARAMETER_REAL}};
static int APO_PLUGIN_CALL evaluate(const ApoPluginInputV1 *input, const double *values,
                                    uint32_t count, ApoPluginOutputV1 *output) {
    if (!input || !output || count != 2 || input->struct_size != sizeof(*input) ||
        output->struct_size != sizeof(*output))
        return 1;
    /* The host accumulates weight * output, so neither += nor a weight multiply
       belongs here. Parameters are immutable values, not offsets into a struct. */
    output->x = values[0] * input->x + values[1];
    output->y = values[0] * input->y;
    return 0;
}
static const ApoPluginV1 descriptor = {sizeof(ApoPluginV1),
                                       APO_PLUGIN_ABI_V1,
                                       "org.apophysis.reborn.migration.gain",
                                       "migrated_gain",
                                       "1.0.0",
                                       "Illustrative source migration of a weighted 2D operation",
                                       2,
                                       parameters,
                                       evaluate};
APO_PLUGIN_EXPORT const ApoPluginV1 *APO_PLUGIN_CALL apo_plugin_query(uint32_t requested_abi) {
    return requested_abi == APO_PLUGIN_ABI_V1 ? &descriptor : 0;
}
