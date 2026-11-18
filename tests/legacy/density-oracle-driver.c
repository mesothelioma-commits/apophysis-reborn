/* Test inputs for the original FLAM3 double-bucket density worker.
 * The harness supplies a complete static histogram and log-exposure constants.
 * All kernel creation, selection, scattering and boundary checks are upstream. */
static void case_grid(const char *name, int os, double radius, double minimum, double curve) {
    enum { width = 39, height = 33, samples = 8192 };
    const double camera_scale = 7;
    bucket bins[height * width] = {{0}};
    abucket accum[height * width] = {{0}};
    flam3_frame frame = {0};
    int aborted = 0;
    flam3_de_helper filter = flam3_create_de_filters(radius, minimum, curve, os);
    if (filter.max_filter_index <= 0) abort();
    printf("case\t%s\t%d\t%d\t%d\t%.17g\t%.17g\t%.17g\t%d\t%.17g\n",
           name, width, height, os, radius, minimum, curve, samples, camera_scale);
    /* Distinct colors expose channel mixing, neighboring impulses test oversample
       hit selection, and dense bins span the compressed count lookup. */
    const int hits[][6] = {
        {2, 2, 1, 255, 0, 51}, {11, 13, 2, 51, 255, 0},
        {12, 14, 3, 0, 51, 255}, {29, 6, 1, 255, 255, 153},
        {8, 25, 100, 51, 153, 255}, {20, 25, 105, 255, 204, 0},
        {32, 24, 2600, 102, 255, 255}, {37, 31, 1, 204, 0, 102}
    };
    for (size_t i = 0; i < sizeof(hits)/sizeof(hits[0]); ++i) {
        const int *row = hits[i];
        const int x = row[0], y = row[1], n = row[2];
        for (int k = 0; k < 3; ++k) bins[x + y*width][k] = n * row[k + 3];
        bins[x + y*width][3] = bins[x + y*width][4] = n * 255;
        printf("bin\t%d\t%d\t%d\t%d\t%d\t%d\n", x, y, n, row[3], row[4], row[5]);
    }
    de_thread_helper task = {0};
    task.b = bins; task.accumulate = accum; task.de = &filter; task.spec = &frame;
    task.width = width; task.height = height; task.oversample = os;
    task.end_row = height - 2*(os - 1); task.aborted = &aborted;
    task.k1 = 268.0 / 256.0;
    task.k2 = os * os * camera_scale * camera_scale / (255 * samples);
    task.curve = curve;
    de_thread(&task);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double *v = accum[x+y*width];
            if (v[3] != 0)
                printf("light\t%d\t%d\t%.17g\t%.17g\t%.17g\t%.17g\n", x, y, v[0], v[1], v[2], v[3]);
        }
    }
    puts("end");
    free(filter.filter_coefs); free(filter.filter_widths);
}

int main(void) {
    case_grid("wide", 1, 9, 0, .4);
    case_grid("even", 2, 2.25, .4, .55);
    case_grid("odd", 3, 1.6, .3, .4);
    case_grid("rolling-even", 4, 1.25, .25, .5);
    case_grid("rolling-odd", 5, 1.4, .2, .47);
    return 0;
}
