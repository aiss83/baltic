/*
 * baltic_embedded - integer-only height conversion for small MCUs.
 *
 * Copyright (c) 2026 Baltic contributors
 * SPDX-License-Identifier: MIT
 */

#include "baltic_embedded.h"

#include <stddef.h>

#define BALTIC_LON_LIMIT 180000000L
#define BALTIC_LON_FULL 360000000L
#define BALTIC_Q8 256L
#define BALTIC_BIAS 32768L

static int32_t normalize_longitude(int32_t lon_u6) {
    lon_u6 %= BALTIC_LON_FULL;
    if (lon_u6 > BALTIC_LON_LIMIT)
        lon_u6 -= BALTIC_LON_FULL;
    else if (lon_u6 < -BALTIC_LON_LIMIT)
        lon_u6 += BALTIC_LON_FULL;
    return lon_u6;
}

static baltic_embedded_status check_grid(const baltic_embedded_grid *grid) {
    if (grid == NULL || grid->values == NULL)
        return BALTIC_EMBEDDED_ERR_ARGUMENT;
    if (grid->width < 2 || grid->height < 2 || grid->lon_step_u6 <= 0 ||
        grid->lat_step_u6 <= 0 || grid->lon_max_u6 <= grid->lon_min_u6 ||
        grid->lat_max_u6 <= grid->lat_min_u6)
        return BALTIC_EMBEDDED_ERR_GRID;
    return BALTIC_EMBEDDED_OK;
}

int baltic_embedded_grid_contains(const baltic_embedded_grid *grid,
                                  int32_t lat_u6, int32_t lon_u6) {
    int32_t lon;

    if (grid == NULL || grid->values == NULL || grid->width < 2 ||
        grid->height < 2 || lat_u6 < -90000000L || lat_u6 > 90000000L)
        return 0;
    lon = normalize_longitude(lon_u6);
    return lon >= grid->lon_min_u6 && lon <= grid->lon_max_u6 &&
           lat_u6 >= grid->lat_min_u6 && lat_u6 <= grid->lat_max_u6;
}

/* Biased sample in [0, 65535]; interpolation stays non-negative. */
static int32_t sample_biased(const baltic_embedded_grid *grid, int32_t ix,
                             int32_t iy) {
    uint32_t index = (uint32_t)iy * (uint32_t)grid->width + (uint32_t)ix;

    return (int32_t)grid->values[index] + BALTIC_BIAS;
}

static baltic_embedded_status interpolate_mm(const baltic_embedded_grid *grid,
                                             int32_t lat_u6, int32_t lon_u6,
                                             int32_t *out_mm) {
    int32_t lon;
    int32_t dx;
    int32_t dy;
    int32_t ix;
    int32_t iy;
    int32_t fx;
    int32_t fy;
    int32_t top;
    int32_t bottom;
    int32_t value;
    baltic_embedded_status status = check_grid(grid);

    if (status != BALTIC_EMBEDDED_OK)
        return status;
    if (lat_u6 < -90000000L || lat_u6 > 90000000L)
        return BALTIC_EMBEDDED_ERR_ARGUMENT;

    lon = normalize_longitude(lon_u6);
    if (lon < grid->lon_min_u6 || lon > grid->lon_max_u6 ||
        lat_u6 < grid->lat_min_u6 || lat_u6 > grid->lat_max_u6)
        return BALTIC_EMBEDDED_ERR_OUTSIDE;

    dx = lon - grid->lon_min_u6;
    ix = dx / grid->lon_step_u6;
    if (ix >= (int32_t)grid->width - 1) {
        ix = (int32_t)grid->width - 2;
        fx = BALTIC_Q8;
    } else {
        fx = ((dx - ix * grid->lon_step_u6) * BALTIC_Q8) / grid->lon_step_u6;
    }

    dy = lat_u6 - grid->lat_min_u6;
    iy = dy / grid->lat_step_u6;
    if (iy >= (int32_t)grid->height - 1) {
        iy = (int32_t)grid->height - 2;
        fy = BALTIC_Q8;
    } else {
        fy = ((dy - iy * grid->lat_step_u6) * BALTIC_Q8) / grid->lat_step_u6;
    }

    /* Both samples and weights are non-negative, so shifts are safe. */
    top = (sample_biased(grid, ix, iy) * (BALTIC_Q8 - fx) +
           sample_biased(grid, ix + 1, iy) * fx + BALTIC_Q8 / 2) >>
          (8);
    bottom = (sample_biased(grid, ix, iy + 1) * (BALTIC_Q8 - fx) +
              sample_biased(grid, ix + 1, iy + 1) * fx + BALTIC_Q8 / 2) >>
             (8);
    value = (top * (BALTIC_Q8 - fy) + bottom * fy + BALTIC_Q8 / 2) >> (8);

    *out_mm = value - BALTIC_BIAS + grid->base_mm;
    return BALTIC_EMBEDDED_OK;
}

baltic_embedded_status
baltic_embedded_geoid_mm(const baltic_embedded_grid *grid, int32_t lat_u6,
                         int32_t lon_u6, int32_t *n_mm) {
    int32_t value;
    baltic_embedded_status status;

    if (n_mm == NULL)
        return BALTIC_EMBEDDED_ERR_ARGUMENT;
    status = interpolate_mm(grid, lat_u6, lon_u6, &value);
    if (status == BALTIC_EMBEDDED_OK)
        *n_mm = value;
    return status;
}

static baltic_embedded_status add_correction(
    const baltic_embedded_grid *correction, int32_t lat_u6, int32_t lon_u6,
    int64_t *value, int direction) {
    int32_t correction_mm;
    baltic_embedded_status status;

    if (correction == NULL)
        return BALTIC_EMBEDDED_OK;
    status = interpolate_mm(correction, lat_u6, lon_u6, &correction_mm);
    if (status != BALTIC_EMBEDDED_OK)
        return status;
    *value += (int64_t)direction * correction_mm;
    return BALTIC_EMBEDDED_OK;
}

static baltic_embedded_status store_result(int64_t value, int32_t *out_mm) {
    if (value > 2147483647LL || value < -2147483648LL)
        return BALTIC_EMBEDDED_ERR_ARGUMENT;
    *out_mm = (int32_t)value;
    return BALTIC_EMBEDDED_OK;
}

baltic_embedded_status
baltic_embedded_h_to_H(const baltic_embedded_grid *geoid,
                       const baltic_embedded_grid *correction, int32_t lat_u6,
                       int32_t lon_u6, int32_t h_mm, int32_t offset_mm,
                       int32_t *H_mm) {
    int32_t n_mm;
    int64_t value;
    baltic_embedded_status status;

    if (H_mm == NULL)
        return BALTIC_EMBEDDED_ERR_ARGUMENT;
    status = baltic_embedded_geoid_mm(geoid, lat_u6, lon_u6, &n_mm);
    if (status != BALTIC_EMBEDDED_OK)
        return status;

    value = (int64_t)h_mm - n_mm + offset_mm;
    status = add_correction(correction, lat_u6, lon_u6, &value, +1);
    if (status != BALTIC_EMBEDDED_OK)
        return status;
    return store_result(value, H_mm);
}

baltic_embedded_status
baltic_embedded_H_to_h(const baltic_embedded_grid *geoid,
                       const baltic_embedded_grid *correction, int32_t lat_u6,
                       int32_t lon_u6, int32_t H_mm, int32_t offset_mm,
                       int32_t *h_mm) {
    int32_t n_mm;
    int64_t value;
    baltic_embedded_status status;

    if (h_mm == NULL)
        return BALTIC_EMBEDDED_ERR_ARGUMENT;
    status = baltic_embedded_geoid_mm(geoid, lat_u6, lon_u6, &n_mm);
    if (status != BALTIC_EMBEDDED_OK)
        return status;

    value = (int64_t)H_mm - offset_mm + n_mm;
    status = add_correction(correction, lat_u6, lon_u6, &value, -1);
    if (status != BALTIC_EMBEDDED_OK)
        return status;
    return store_result(value, h_mm);
}
