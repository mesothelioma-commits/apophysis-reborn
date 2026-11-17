// Input and output driver appended to the external FLAM3 numerical functions.
// This file is used only by reference_generate.py.
static void emit(int kind, double x, double y, double old_color, int bit) {
    flam3_xform xf = {0};
    xf.precalc_atan_xy_flag = xf.precalc_atan_yx_flag = xf.precalc_angles_flag = 1;
    xf.num_active_vars = 1;
    xf.varFunc[0] = kind;
    xf.active_var_weights[0] = 1;
    xf.color = .75;
    xf.color_speed = .25;
    xf.vis_adjusted = 1;
    xf.c[0][0] = xf.c[1][1] = 1;
    if (kind == 15) {
        // First xform in FLAM3's test.flam3, including its default color speed.
        double c[3][2] = {{-.681206, -.0779465}, {.20769, .755065}, {-.0416126, -.262334}};
        memcpy(xf.c, c, sizeof c);
        xf.color = 1;
        xf.color_speed = .5;
        xf.varFunc[0] = VAR_SPHERICAL;
    } else if (kind == 16) {
        double c[3][2] = {{.3, -.4}, {.25, .7}, {.1, .6}};
        double post[3][2] = {{.9, .35}, {-.65, .3}, {-.25, .2}};
        memcpy(xf.c, c, sizeof c);
        memcpy(xf.post, post, sizeof post);
        xf.has_post = 1;
        xf.num_active_vars = 3;
        xf.varFunc[0] = VAR_LINEAR;
        xf.varFunc[1] = VAR_SPHERICAL;
        xf.varFunc[2] = VAR_DISC;
        xf.active_var_weights[0] = .7;
        xf.active_var_weights[1] = -.2;
        xf.active_var_weights[2] = .35;
    }
    double input[4] = {x, y, old_color, 1}, q[4];
    flam3_genome g = {&xf};
    if (apply_xform(&g, 0, input, q, &bit) != 0) abort();
    printf("%d\t%.17g\t%.17g\t%.17g\t%d\t%.17g\t%.17g\t%.17g\n",
           kind, x, y, old_color, bit, q[0], q[1], q[2]);
}

int main(void) {
    const double points[][2] = {{.3, .7}, {-.31, .73}, {-.78, -.27}, {.45, -.86},
                                 {.0, -.75}, {.7, .0}, {1e-6, -2e-6}};
    puts("kind\tx\ty\told_color\tbranch\texpected_x\texpected_y\texpected_color");
    for (int kind = 0; kind <= 16; ++kind) {
        for (size_t i = 0; i < sizeof points / sizeof points[0]; ++i) {
            emit(kind, points[i][0], points[i][1], .125, 0);
            if (kind == VAR_JULIA) emit(kind, points[i][0], points[i][1], .125, 1);
        }
    }
    return 0;
}
