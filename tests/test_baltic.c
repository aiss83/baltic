/*
 * Unit tests for the baltic library using a synthetic 3x3 GTX grid with a
 * linear geoid field.  Bilinear interpolation of a linear field is exact,
 * so expected values are known analytically.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "baltic/baltic.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
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

static void put_be64(unsigned char *p, double value) {
    uint64_t bits;
    int i;

    memcpy(&bits, &value, sizeof(bits));
    for (i = 0; i < 8; ++i)
        p[i] = (unsigned char)(bits >> (56 - 8 * i));
}

static void put_be32(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static void put_be_float(unsigned char *p, float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    put_be32(p, bits);
}

/* N(lon, lat) = 10 + 2*(lon-10) + 3*(lat-50) */
static double test_undulation(double lon, double lat) {
    return 10.0 + 2.0 * (lon - 10.0) + 3.0 * (lat - 50.0);
}

static int write_test_grid(const char *path) {
    const double lat0 = 50.0;
    const double lon0 = 10.0;
    const double dlat = 1.0;
    const double dlon = 1.0;
    const int rows = 3;
    const int cols = 3;
    unsigned char header[40];
    unsigned char row[3 * 4];
    FILE *fp;
    int j;

    memset(header, 0, sizeof(header));
    put_be64(header + 0, lat0);
    put_be64(header + 8, lon0);
    put_be64(header + 16, dlat);
    put_be64(header + 24, dlon);
    put_be32(header + 32, (uint32_t)rows);
    put_be32(header + 36, (uint32_t)cols);

    fp = fopen(path, "wb");
    if (fp == NULL)
        return -1;
    if (fwrite(header, 1, sizeof(header), fp) != sizeof(header)) {
        fclose(fp);
        return -1;
    }
    for (j = 0; j < rows; ++j) {
        int i;
        double lat = lat0 + (double)j * dlat;

        for (i = 0; i < cols; ++i) {
            double lon = lon0 + (double)i * dlon;
            put_be_float(row + (size_t)i * 4,
                         (float)test_undulation(lon, lat));
        }
        if (fwrite(row, 1, (size_t)cols * 4, fp) != (size_t)cols * 4) {
            fclose(fp);
            return -1;
        }
    }
    return fclose(fp) == 0 ? 0 : -1;
}

static void test_grid_info(baltic_ctx *ctx) {
    baltic_grid_info info;

    CHECK(baltic_ctx_grid_info(ctx, &info) == BALTIC_OK);
    CHECK(info.width == 3);
    CHECK(info.height == 3);
    CHECK_NEAR(info.west_deg, 10.0, 1e-9);
    CHECK_NEAR(info.south_deg, 50.0, 1e-9);
    CHECK_NEAR(info.east_deg, 12.0, 1e-9);
    CHECK_NEAR(info.north_deg, 52.0, 1e-9);
    CHECK_NEAR(info.dlon_deg, 1.0, 1e-9);
    CHECK_NEAR(info.dlat_deg, 1.0, 1e-9);
    CHECK(strstr(info.format, "gtx") != NULL ||
          strstr(info.format, "GTX") != NULL);

    CHECK(baltic_ctx_grid_info(ctx, NULL) == BALTIC_ERR_INVALID_ARG);
}

static void test_conversion(baltic_ctx *ctx) {
    double h = 100.0;
    double H = 0.0;
    baltic_conversion_info info;

    CHECK(baltic_ellipsoidal_to_baltic(ctx, 52.0, 12.0, h, &H, &info) ==
          BALTIC_OK);
    CHECK_NEAR(H, 80.0, 1e-5);
    CHECK_NEAR(info.geoid_undulation_m, 20.0, 1e-5);
    CHECK_NEAR(info.correction_m, 0.0, 1e-9);
    CHECK_NEAR(info.datum_offset_m, 0.0, 1e-9);

    CHECK(baltic_ellipsoidal_to_baltic(ctx, 50.5, 10.5, h, &H, &info) ==
          BALTIC_OK);
    CHECK_NEAR(H, 87.5, 1e-5);
    CHECK_NEAR(info.geoid_undulation_m, 12.5, 1e-5);

    /* Longitude wrap-around must produce the same point. */
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 50.5, 370.5, h, &H, &info) ==
          BALTIC_OK);
    CHECK_NEAR(H, 87.5, 1e-5);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 50.5, -349.5, h, &H, &info) ==
          BALTIC_OK);
    CHECK_NEAR(H, 87.5, 1e-5);

    CHECK(baltic_ctx_set_offset(ctx, 1.25) == BALTIC_OK);
    CHECK_NEAR(baltic_ctx_get_offset(ctx), 1.25, 1e-12);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 50.5, 10.5, h, &H, &info) ==
          BALTIC_OK);
    CHECK_NEAR(H, 88.75, 1e-5);
    CHECK_NEAR(info.datum_offset_m, 1.25, 1e-12);

    {
        double back = 0.0;

        CHECK(baltic_baltic_to_ellipsoidal(ctx, 50.5, 10.5, 88.75, &back,
                                           &info) == BALTIC_OK);
        CHECK_NEAR(back, 100.0, 1e-5);
        CHECK_NEAR(info.geoid_undulation_m, 12.5, 1e-5);
        CHECK_NEAR(info.datum_offset_m, 1.25, 1e-12);
    }

    CHECK(baltic_ctx_set_offset(ctx, 0.0) == BALTIC_OK);

    /* Outside coverage. */
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 51.0, 13.0, h, &H, &info) ==
          BALTIC_ERR_OUT_OF_GRID);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 51.0, 9.9, h, &H, &info) ==
          BALTIC_ERR_OUT_OF_GRID);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 52.1, 11.0, h, &H, &info) ==
          BALTIC_ERR_OUT_OF_GRID);

    /* Invalid arguments. */
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 91.0, 11.0, h, &H, &info) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, -91.0, 11.0, h, &H, &info) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 51.0, 11.0, NAN, &H, &info) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 51.0, 11.0, h, NULL, &info) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_ellipsoidal_to_baltic(NULL, 51.0, 11.0, h, &H, &info) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_convert(ctx, (baltic_direction)0, 51.0, 11.0, h, &H, &info) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_ctx_set_offset(ctx, NAN) == BALTIC_ERR_INVALID_ARG);
}

static void test_batch(baltic_ctx *ctx) {
    baltic_batch_point points[3];
    size_t ok;

    memset(points, 0, sizeof(points));
    points[0].lat_deg = 50.5;
    points[0].lon_deg = 10.5;
    points[0].height_in_m = 100.0;
    points[1].lat_deg = 51.0;
    points[1].lon_deg = 13.0;
    points[1].height_in_m = 100.0;
    points[2].lat_deg = 51.0;
    points[2].lon_deg = 11.0;
    points[2].height_in_m = 100.0;

    ok = baltic_convert_batch(ctx, BALTIC_ELLIPSOIDAL_TO_BALTIC, points, 3);
    CHECK(ok == 2);
    CHECK(points[0].status == BALTIC_OK);
    CHECK_NEAR(points[0].height_out_m, 87.5, 1e-5);
    CHECK_NEAR(points[0].geoid_undulation_m, 12.5, 1e-5);
    CHECK(points[1].status == BALTIC_ERR_OUT_OF_GRID);
    CHECK(isnan(points[1].height_out_m));
    CHECK(points[2].status == BALTIC_OK);
    CHECK_NEAR(points[2].height_out_m, 85.0, 1e-5);

    CHECK(baltic_convert_batch(ctx, BALTIC_ELLIPSOIDAL_TO_BALTIC, points, 0) ==
          0);
    CHECK(baltic_convert_batch(ctx, BALTIC_ELLIPSOIDAL_TO_BALTIC, NULL, 3) ==
          0);
}

static void test_calibration(baltic_ctx *ctx) {
    baltic_control_point points[4];
    baltic_calibration calibration;

    memset(points, 0, sizeof(points));
    /* h - N + 0.8 */
    points[0].lat_deg = 50.5;
    points[0].lon_deg = 10.5;
    points[0].height_ellipsoidal_m = 100.0;
    points[0].height_baltic_m = 100.0 - 12.5 + 0.8;

    points[1].lat_deg = 51.0;
    points[1].lon_deg = 11.0;
    points[1].height_ellipsoidal_m = 120.0;
    points[1].height_baltic_m = 120.0 - 15.0 + 0.8;

    points[2].lat_deg = 51.5;
    points[2].lon_deg = 11.5;
    points[2].height_ellipsoidal_m = 140.0;
    points[2].height_baltic_m = 140.0 - 17.5 + 0.8;

    CHECK(baltic_calibrate_offset(ctx, points, 3, &calibration) == BALTIC_OK);
    CHECK_NEAR(calibration.offset_m, 0.8, 1e-6);
    CHECK_NEAR(calibration.rms_m, 0.0, 1e-6);
    CHECK(calibration.used_points == 3);
    CHECK(calibration.skipped_points == 0);

    /* An out-of-grid point is skipped. */
    points[3].lat_deg = 40.0;
    points[3].lon_deg = 40.0;
    points[3].height_ellipsoidal_m = 50.0;
    points[3].height_baltic_m = 40.0;
    CHECK(baltic_calibrate_offset(ctx, points, 4, &calibration) == BALTIC_OK);
    CHECK_NEAR(calibration.offset_m, 0.8, 1e-6);
    CHECK(calibration.used_points == 3);
    CHECK(calibration.skipped_points == 1);

    CHECK(baltic_calibrate_offset(ctx, points, 0, &calibration) ==
          BALTIC_ERR_INVALID_ARG);
    CHECK(baltic_calibrate_offset(ctx, NULL, 3, &calibration) ==
          BALTIC_ERR_INVALID_ARG);
}

static void test_correction(const char *grid_path) {
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    double H = 0.0;
    double h = 0.0;
    baltic_conversion_info info;

    config.geoid_grid = grid_path;
    config.correction_grid = grid_path;
    config.datum_offset_m = 0.0;

    CHECK(baltic_ctx_create(&config, &ctx) == BALTIC_OK);
    if (ctx == NULL)
        return;

    /* Correction equals N, so H = h. */
    CHECK(baltic_ellipsoidal_to_baltic(ctx, 51.0, 11.0, 100.0, &H, &info) ==
          BALTIC_OK);
    CHECK_NEAR(H, 100.0, 1e-5);
    CHECK_NEAR(info.geoid_undulation_m, 15.0, 1e-5);
    CHECK_NEAR(info.correction_m, 15.0, 1e-5);

    CHECK(baltic_baltic_to_ellipsoidal(ctx, 51.0, 11.0, 100.0, &h, &info) ==
          BALTIC_OK);
    CHECK_NEAR(h, 100.0, 1e-5);

    baltic_ctx_destroy(ctx);
}

typedef struct thread_args {
    baltic_ctx *ctx;
    int iterations;
    int failures;
} thread_args;

static void *thread_worker(void *opaque) {
    thread_args *args = (thread_args *)opaque;
    int i;

    for (i = 0; i < args->iterations; ++i) {
        double H = 0.0;

        if (baltic_ellipsoidal_to_baltic(args->ctx, 50.5, 10.5, 100.0, &H,
                                         NULL) != BALTIC_OK ||
            fabs(H - 87.5) > 1e-5) {
            ++args->failures;
        }
    }
    return NULL;
}

static void test_threads(baltic_ctx *ctx) {
    enum { THREADS = 4, ITERATIONS = 1000 };
    pthread_t threads[THREADS];
    thread_args args[THREADS];
    int i;

    for (i = 0; i < THREADS; ++i) {
        args[i].ctx = ctx;
        args[i].iterations = ITERATIONS;
        args[i].failures = 0;
        CHECK(pthread_create(&threads[i], NULL, thread_worker, &args[i]) == 0);
    }
    for (i = 0; i < THREADS; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    for (i = 0; i < THREADS; ++i)
        CHECK(args[i].failures == 0);
}

static void test_context_errors(const char *grid_path, const char *tmp_dir) {
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    char bad_grid[512];
    FILE *fp;

    CHECK(baltic_ctx_create(NULL, &ctx) == BALTIC_ERR_INVALID_ARG);
    CHECK(ctx == NULL);

    config.geoid_grid = "/nonexistent/baltic/egm2008.tif";
    CHECK(baltic_ctx_create(&config, &ctx) == BALTIC_ERR_GRID_NOT_FOUND);

    snprintf(bad_grid, sizeof(bad_grid), "%s/bad.txt", tmp_dir);
    fp = fopen(bad_grid, "w");
    CHECK(fp != NULL);
    if (fp != NULL) {
        fputs("not a grid\n", fp);
        fclose(fp);
    }
    config.geoid_grid = bad_grid;
    CHECK(baltic_ctx_create(&config, &ctx) == BALTIC_ERR_GRID_INVALID);

    config.geoid_grid = grid_path;
    config.datum_offset_m = NAN;
    CHECK(baltic_ctx_create(&config, &ctx) == BALTIC_ERR_INVALID_ARG);
}

static void test_search_path(const char *tmp_dir) {
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;
    double H = 0.0;

    config.geoid_grid = "test.gtx";
    config.grid_search_path = tmp_dir;
    CHECK(baltic_ctx_create(&config, &ctx) == BALTIC_OK);
    if (ctx != NULL) {
        CHECK(baltic_ellipsoidal_to_baltic(ctx, 50.5, 10.5, 100.0, &H, NULL) ==
              BALTIC_OK);
        CHECK_NEAR(H, 87.5, 1e-5);
        baltic_ctx_destroy(ctx);
    }
}

static void test_versions(void) {
    CHECK(strlen(baltic_version()) > 0);
    CHECK(strlen(baltic_proj_version()) > 0);
    CHECK(strcmp(baltic_status_string(BALTIC_OK), "ok") == 0);
    CHECK(strcmp(baltic_status_string(BALTIC_ERR_OUT_OF_GRID),
                 "point is outside grid coverage") == 0);
}

int main(void) {
    char tmp_template[] = "/tmp/baltic-test-XXXXXX";
    char *tmp_dir;
    char grid_path[512];
    baltic_config config = BALTIC_CONFIG_INIT;
    baltic_ctx *ctx = NULL;

    tmp_dir = mkdtemp(tmp_template);
    if (tmp_dir == NULL) {
        perror("mkdtemp");
        return 2;
    }
    snprintf(grid_path, sizeof(grid_path), "%s/test.gtx", tmp_dir);
    if (write_test_grid(grid_path) != 0) {
        perror("write_test_grid");
        rmdir(tmp_dir);
        return 2;
    }

    config.geoid_grid = grid_path;
    CHECK(baltic_ctx_create(&config, &ctx) == BALTIC_OK);
    if (ctx != NULL) {
        test_grid_info(ctx);
        test_conversion(ctx);
        test_batch(ctx);
        test_calibration(ctx);
        test_threads(ctx);
        baltic_ctx_destroy(ctx);
    }

    test_correction(grid_path);
    test_context_errors(grid_path, tmp_dir);
    test_search_path(tmp_dir);
    test_versions();

    remove(grid_path);
    {
        char bad_path[512];

        snprintf(bad_path, sizeof(bad_path), "%s/bad.txt", tmp_dir);
        remove(bad_path);
    }
    rmdir(tmp_dir);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
