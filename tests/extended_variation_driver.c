// Input/output harness only. The generator inserts the unmodified external C functions.
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
@EPS@
typedef struct {
    double c[3][2], waves_dx2, waves_dy2;
} flam3_xform;
typedef struct {
    flam3_xform *xform;
    double tx, ty, p0, p1, precalc_atan, precalc_sqrt, precalc_sina, precalc_cosa;
} flam3_iter_helper;
@FUNCTIONS@

int main(void) {
    int kind;
    double a, b, c, d, e, f, weight, x, y;
    while (scanf("%d %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                 &kind, &a, &b, &c, &d, &e, &f, &weight, &x, &y) == 10) {
        flam3_xform xf = {.c = {{a, b}, {c, d}, {e, f}}};
        xf.waves_dx2 = 1 / (e * e + EPS);
        xf.waves_dy2 = 1 / (f * f + EPS);
        flam3_iter_helper helper = {.xform = &xf, .tx = a * x + c * y + e,
                                    .ty = b * x + d * y + f};
        helper.precalc_sqrt = sqrt(helper.tx * helper.tx + helper.ty * helper.ty);
        helper.precalc_atan = atan2(helper.tx, helper.ty);
        helper.precalc_sina = helper.tx / helper.precalc_sqrt;
        helper.precalc_cosa = helper.ty / helper.precalc_sqrt;
        switch (kind) {
            @CALLS@
            default: abort();
        }
        // A fixed asymmetric post-affine makes omission/order errors observable.
        printf("%.17g\t%.17g\n", .91 * helper.p0 + .27 * helper.p1 - .05,
                                   -.13 * helper.p0 + .75 * helper.p1 + .08);
    }
    return 0;
}
