/*
 * Host tests for the integer-only embedded library.
 *
 * Golden geoid values are from the full EGM2008 grid, rounded to
 * millimetres.  The generated grids use int16 millimetres relative to a
 * per-grid base, so a tolerance of 2 mm covers quantization and
 * interpolation rounding.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#include "baltic_embedded.h"
#include "egm2008_moscow_2deg.h"
#include "egm2008_spb_2deg.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

#define CHECK_INT_NEAR(actual, expected, tolerance)                            \
    do {                                                                       \
        int32_t a_ = (actual);                                                 \
        int32_t e_ = (expected);                                               \
        int32_t t_ = (tolerance);                                              \
        ++g_checks;                                                            \
        if (a_ < e_ - t_ || a_ > e_ + t_) {                                    \
            ++g_failures;                                                      \
            fprintf(stderr,                                                    \
                    "FAIL %s:%d: %s = %ld, expected %ld +/- %ld\n", __FILE__,  \
                    __LINE__, #actual, (long)a_, (long)e_, (long)t_);          \
        }                                                                      \
    } while (0)

typedef struct sample {
    int32_t lat_u6;
    int32_t lon_u6;
    int32_t n_mm;
} sample;

static const sample moscow_samples[] = {
    {55755800, 37617300, 14542},
    {54000000, 36000000, 14854},
    {56000000, 38000000, 14725},
    {55000000, 37000000, 15211},
    {55250000, 37250000, 15014},
    {54500000, 36500000, 14727},
    {55900000, 37900000, 14591},
};

static const sample spb_samples[] = {
    {59950000, 30316700, 15951},
    {60000000, 29766700, 15741},
    {59000000, 29000000, 15850},
    {61000000, 31000000, 16264},
    {60500000, 30500000, 15942},
};

static void check_samples(const baltic_embedded_grid *grid,
                          const sample *samples, size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) {
        int32_t n = 0;
        baltic_embedded_status status = baltic_embedded_geoid_mm(
            grid, samples[i].lat_u6, samples[i].lon_u6, &n);

        CHECK(status == BALTIC_EMBEDDED_OK);
        CHECK_INT_NEAR(n, samples[i].n_mm, 2);
    }
}

static void test_metadata(const baltic_embedded_grid *moscow,
                          const baltic_embedded_grid *spb) {
    CHECK(moscow->width == 49);
    CHECK(moscow->height == 49);
    CHECK(moscow->lon_min_u6 == 36000000);
    CHECK(moscow->lat_min_u6 == 54000000);
    CHECK(moscow->lon_step_u6 == 41667);
    CHECK(moscow->lat_step_u6 == 41667);

    CHECK(spb->width == 49);
    CHECK(spb->height == 49);
    CHECK(spb->lon_min_u6 == 29000000);
    CHECK(spb->lat_min_u6 == 59000000);

    CHECK(baltic_embedded_grid_contains(moscow, 55755800, 37617300) == 1);
    CHECK(baltic_embedded_grid_contains(moscow, 54000000, 36000000) == 1);
    CHECK(baltic_embedded_grid_contains(moscow, 56000000, 38000000) == 1);
    CHECK(baltic_embedded_grid_contains(moscow, 40000000, 40000000) == 0);
    CHECK(baltic_embedded_grid_contains(moscow, 55755800,
                                        37617300 + 360000000) == 1);
    CHECK(baltic_embedded_grid_contains(NULL, 55000000, 37000000) == 0);
}

static void test_conversion(const baltic_embedded_grid *grid) {
    const int32_t lat = 55755800;
    const int32_t lon = 37617300;
    const int32_t h_mm = 200000;
    const int32_t offset_mm = 500;
    int32_t n = 0;
    int32_t H = 0;
    int32_t back = 0;
    baltic_embedded_status status;

    CHECK(baltic_embedded_geoid_mm(grid, lat, lon, &n) == BALTIC_EMBEDDED_OK);

    status = baltic_embedded_h_to_H(grid, NULL, lat, lon, h_mm, offset_mm, &H);
    CHECK(status == BALTIC_EMBEDDED_OK);
    CHECK_INT_NEAR(H, h_mm - n + offset_mm, 0);

    status = baltic_embedded_H_to_h(grid, NULL, lat, lon, H, offset_mm, &back);
    CHECK(status == BALTIC_EMBEDDED_OK);
    CHECK_INT_NEAR(back, h_mm, 0);

    /* Negative ellipsoidal height. */
    status = baltic_embedded_h_to_H(grid, NULL, lat, lon, -100000, 0, &H);
    CHECK(status == BALTIC_EMBEDDED_OK);
    CHECK_INT_NEAR(H, -100000 - n, 0);

    /* Correction equal to the geoid cancels N: H = h + offset. */
    status = baltic_embedded_h_to_H(grid, grid, lat, lon, h_mm, offset_mm, &H);
    CHECK(status == BALTIC_EMBEDDED_OK);
    CHECK_INT_NEAR(H, h_mm + offset_mm, 1);
    status = baltic_embedded_H_to_h(grid, grid, lat, lon, H, offset_mm, &back);
    CHECK(status == BALTIC_EMBEDDED_OK);
    CHECK_INT_NEAR(back, h_mm, 1);

    /* Longitude wrap-around. */
    status = baltic_embedded_h_to_H(grid, NULL, lat, lon + 360000000, h_mm,
                                    offset_mm, &H);
    CHECK(status == BALTIC_EMBEDDED_OK);
    CHECK_INT_NEAR(H, h_mm - n + offset_mm, 0);
}

static void test_edges(const baltic_embedded_grid *grid) {
    int32_t n = 0;
    int32_t H = 0;

    CHECK(baltic_embedded_geoid_mm(grid, grid->lat_max_u6, grid->lon_max_u6,
                                   &n) == BALTIC_EMBEDDED_OK);
    CHECK(baltic_embedded_geoid_mm(grid, grid->lat_max_u6 + 1,
                                   grid->lon_min_u6, &n) ==
          BALTIC_EMBEDDED_ERR_OUTSIDE);
    CHECK(baltic_embedded_geoid_mm(grid, grid->lat_min_u6,
                                   grid->lon_max_u6 + 1, &n) ==
          BALTIC_EMBEDDED_ERR_OUTSIDE);
    CHECK(baltic_embedded_geoid_mm(grid, 91000000, grid->lon_min_u6, &n) ==
          BALTIC_EMBEDDED_ERR_ARGUMENT);
    CHECK(baltic_embedded_geoid_mm(grid, -91000000, grid->lon_min_u6, &n) ==
          BALTIC_EMBEDDED_ERR_ARGUMENT);
    CHECK(baltic_embedded_geoid_mm(grid, grid->lat_min_u6, grid->lon_min_u6,
                                   NULL) == BALTIC_EMBEDDED_ERR_ARGUMENT);

    CHECK(baltic_embedded_h_to_H(grid, NULL, 40000000, 40000000, 0, 0, &H) ==
          BALTIC_EMBEDDED_ERR_OUTSIDE);
    CHECK(baltic_embedded_h_to_H(NULL, NULL, 55000000, 37000000, 0, 0, &H) ==
          BALTIC_EMBEDDED_ERR_ARGUMENT);
    CHECK(baltic_embedded_h_to_H(grid, NULL, 55000000, 37000000, 0, 0, NULL) ==
          BALTIC_EMBEDDED_ERR_ARGUMENT);

    /* A correction grid outside its own region fails independently. */
    CHECK(baltic_embedded_h_to_H(grid, &egm2008_spb_2deg, 55000000, 37000000,
                                 0, 0, &H) == BALTIC_EMBEDDED_ERR_OUTSIDE);
    CHECK(baltic_embedded_h_to_H(&egm2008_spb_2deg, grid, 60000000, 30000000,
                                 0, 0, &H) == BALTIC_EMBEDDED_ERR_OUTSIDE);
}

static void test_invalid_grid(void) {
    static const int16_t values[4] = {0, 0, 0, 0};
    baltic_embedded_grid grid = {
        values, 1, 1, 0, 0, 0, 1000000, 1000000, 1000000, 1000000};
    int32_t n = 0;

    CHECK(baltic_embedded_geoid_mm(&grid, 0, 0, &n) ==
          BALTIC_EMBEDDED_ERR_GRID);
    grid.width = 2;
    grid.height = 2;
    grid.lon_step_u6 = 0;
    CHECK(baltic_embedded_geoid_mm(&grid, 0, 0, &n) ==
          BALTIC_EMBEDDED_ERR_GRID);
}

int main(void) {
    test_metadata(&egm2008_moscow_2deg, &egm2008_spb_2deg);
    check_samples(&egm2008_moscow_2deg, moscow_samples,
                  sizeof(moscow_samples) / sizeof(moscow_samples[0]));
    check_samples(&egm2008_spb_2deg, spb_samples,
                  sizeof(spb_samples) / sizeof(spb_samples[0]));
    test_conversion(&egm2008_moscow_2deg);
    test_edges(&egm2008_moscow_2deg);
    test_invalid_grid();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
