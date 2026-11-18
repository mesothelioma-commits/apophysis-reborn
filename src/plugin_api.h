#ifndef APOPHYSIS_REBORN_PLUGIN_API_H
#define APOPHYSIS_REBORN_PLUGIN_API_H
/* Apophysis Reborn variation ABI v1. This header is original Reborn code.
 * Modules are native trusted code, not a sandbox. No C++ objects cross this ABI.
 * Every callback must be reentrant and must return; no retained input pointers.
 * A module exports apo_plugin_query. Output is an UNWEIGHTED 2D contribution;
 * the renderer applies the term weight exactly once after this callback.
 */
#include <stdint.h>
#ifdef _WIN32
#define APO_PLUGIN_EXPORT __declspec(dllexport)
#define APO_PLUGIN_CALL __cdecl
#else
#define APO_PLUGIN_EXPORT __attribute__((visibility("default")))
#define APO_PLUGIN_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define APO_PLUGIN_ABI_V1 1u
#define APO_PLUGIN_PARAMETER_REAL 0u
#define APO_PLUGIN_PARAMETER_INTEGER 1u

typedef struct ApoPluginParameterV1 {
    const char *name;
    double default_value;
    double minimum;
    double maximum;
    uint32_t kind;
} ApoPluginParameterV1;

typedef struct ApoPluginInputV1 {
    uint32_t struct_size;
    double x, y;
    double affine[6]; /* a,b,c,d,e,f; x'=a*x+c*y+e, y'=b*x+d*y+f */
    double weight;    /* Read-only context; do not multiply the returned point by it. */
    void *random_context;
    double(APO_PLUGIN_CALL *random_unit)(void *context); /* [0,1), current orbit stream */
} ApoPluginInputV1;

typedef struct ApoPluginOutputV1 {
    uint32_t struct_size;
    double x, y;
    char error[256]; /* NUL-terminated detail on failure; no allocation crosses ABI. */
} ApoPluginOutputV1;

typedef struct ApoPluginV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char *id;   /* Stable publisher/module identity. */
    const char *name; /* Unique variation XML name. */
    const char *version;
    const char *description;
    uint32_t parameter_count;
    const ApoPluginParameterV1 *parameters;
    int(APO_PLUGIN_CALL *evaluate)(const ApoPluginInputV1 *input, const double *parameters,
                                   uint32_t count,
                                   ApoPluginOutputV1 *output); /* 0 success, nonzero error */
} ApoPluginV1;

typedef const ApoPluginV1 *(APO_PLUGIN_CALL *ApoPluginQuery)(uint32_t requested_abi);
APO_PLUGIN_EXPORT const ApoPluginV1 *APO_PLUGIN_CALL apo_plugin_query(uint32_t requested_abi);
#ifdef __cplusplus
}
#endif
#endif
