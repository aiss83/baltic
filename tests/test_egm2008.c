/*
 * Integration test against the real EGM2008 2.5' grid.
 *
 * The test is skipped (exit code 77) when the grid is not present.  Point
 * it to a grid with BALTIC_EGM2008_GRID if it is stored elsewhere.
 *
 * Golden values are produced with PROJ's cct using the same grid:
 *
 *   echo "LON LAT 0" | cct -d 12 \
 *       +proj=pipeline \
 *       +step +proj=unitconvert +xy_in=deg +xy_out=rad \
 *       +step +proj=vgridshift +grids=us_nga_egm08_25.tif +multiplier=1 \
 *       +step +proj=unitconvert +xy_in=rad +xy_out=deg
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "baltic/baltic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(actual, expected, eps)                                      \
    do {                                                                       \
        double a_ = (actual);                                                  \
        double e_ = (expected);                                                \
        ++g_checks;                                                            \
        if (!(fabs(a_ - e_) <= (eps))) {                                       \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s = %.12f, expected %.12f\n",        \
                    __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                      \
    } while (0)

typedef struct sample {
    const char *name;
    double lon_deg;
    double lat_deg;
    double n_expected_m;
} sample;

/* Filled in from cct output; see the comment at the top of the file. */
static const sample samples[] = {
    {"prime-meridian-equator", 0.0, 0.0, 17.225084304810},
    {"moscow", 37.6173, 55.7558, 14.541258822817},
    {"saint-petersburg", 30.3167, 59.9500, 15.950649319153},
    {"kronstadt", 29.7667, 60.0000, 15.741388948059},
    {"kaliningrad", 20.5100, 54.7100, 27.023646487427},
    {"sochi", 39.7303, 43.6028, 18.652607619836},
    {"novosibirsk", 82.9204, 55.0302, -36.230655934619},
    {"vladivostok", 131.8855, 43.1155, 26.196279118347},
};

static const char *find_grid(void) {
    const char *env = getenv("BALTIC_EGM2008_GRID");
    static const char *default_path =
        BALTIC_TEST_SOURCE_DIR "/data/egm2008/us_nga_egm08_25.tif";

    if (env != NULL && env[0] != '\0')
        return env;
    return default_path;
}

int main(void) {
    const char *grid = find_grid();
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    baltic_grid_info grid_info;
    size_t i;

    if (access(grid, R_OK) != 0) {
        printf("SKIP: EGM2008 grid not found at %s\n", grid);
        return 77;
    }

    config.geoid_grid = grid;
    config.datum_offset_m = 0.5;
    if (baltic_ctx_create(&config, &ctx) != BALTIC_OK) {
        fprintf(stderr, "cannot create context: %s\n", baltic_last_error());
        return 2;
    }

    CHECK(baltic_ctx_grid_info(ctx, &grid_info) == BALTIC_OK);
    CHECK(grid_info.width > 1000);
    CHECK(grid_info.height > 1000);
    CHECK(grid_info.dlon_deg > 0.0);
    CHECK(grid_info.dlat_deg > 0.0);

    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        baltic_conversion_info info;
        double H = 0.0;
        double h = 0.0;
        double expected_H;

        CHECK(baltic_ellipsoidal_to_baltic(ctx, samples[i].lat_deg,
                                           samples[i].lon_deg, 100.0, &H,
                                           &info) == BALTIC_OK);
        if (fabs(samples[i].n_expected_m) > 0.001) {
            CHECK_NEAR(info.geoid_undulation_m, samples[i].n_expected_m,
                       1e-4);
        }
        expected_H = 100.0 - info.geoid_undulation_m + 0.5;
        CHECK_NEAR(H, expected_H, 1e-9);

        CHECK(baltic_baltic_to_ellipsoidal(ctx, samples[i].lat_deg,
                                           samples[i].lon_deg, H, &h,
                                           NULL) == BALTIC_OK);
        CHECK_NEAR(h, 100.0, 1e-9);

        printf("%-22s N=%12.6f m  H(100 m)=%12.6f m\n", samples[i].name,
               info.geoid_undulation_m, H);
    }

    /* The EGM2008 geoid undulation at (0, 0) is close to 17.16 m. */
    {
        baltic_conversion_info info;
        double H = 0.0;

        CHECK(baltic_ellipsoidal_to_baltic(ctx, 0.0, 0.0, 0.0, &H, &info) ==
              BALTIC_OK);
        CHECK_NEAR(info.geoid_undulation_m, 17.225084, 1e-4);
    }

    /* Calibration must recover a known constant bias. */
    {
        baltic_control_point points[4];
        baltic_calibration calibration;

        memset(points, 0, sizeof(points));
        /* Calibration works with the pure model, without the context offset. */
        CHECK(baltic_ctx_set_offset(ctx, 0.0) == BALTIC_OK);
        for (i = 0; i < 4; ++i) {
            baltic_conversion_info info;
            double model_H;
            double h = 100.0 + 10.0 * (double)i;

            CHECK(baltic_ellipsoidal_to_baltic(ctx, samples[i].lat_deg,
                                               samples[i].lon_deg, h, &model_H,
                                               &info) == BALTIC_OK);
            points[i].lat_deg = samples[i].lat_deg;
            points[i].lon_deg = samples[i].lon_deg;
            points[i].height_ellipsoidal_m = h;
            points[i].height_baltic_m = model_H + 0.25;
        }
        CHECK(baltic_calibrate_offset(ctx, points, 4, &calibration) ==
              BALTIC_OK);
        CHECK_NEAR(calibration.offset_m, 0.25, 1e-6);
        CHECK_NEAR(calibration.rms_m, 0.0, 1e-9);
    }

    baltic_ctx_destroy(ctx);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
